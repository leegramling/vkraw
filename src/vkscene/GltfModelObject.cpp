#include "vkscene/GltfModelObject.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace vkscene {
namespace {

enum class JsonType {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct JsonValue {
    JsonType type = JsonType::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JsonValue> a;
    std::map<std::string, JsonValue> o;

    const JsonValue* get(const std::string& key) const
    {
        if (type != JsonType::Object) return nullptr;
        auto it = o.find(key);
        return (it == o.end()) ? nullptr : &it->second;
    }

    const JsonValue* at(size_t i) const
    {
        if (type != JsonType::Array || i >= a.size()) return nullptr;
        return &a[i];
    }

    std::optional<int64_t> asInt() const
    {
        if (type != JsonType::Number) return std::nullopt;
        return static_cast<int64_t>(n);
    }

    std::optional<double> asNumber() const
    {
        if (type != JsonType::Number) return std::nullopt;
        return n;
    }

    std::optional<std::string> asString() const
    {
        if (type != JsonType::String) return std::nullopt;
        return s;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text)
        : text_(text) {}

    JsonValue parse()
    {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (pos_ != text_.size()) throw std::runtime_error("unexpected trailing json text");
        return v;
    }

private:
    JsonValue parseValue()
    {
        skipWs();
        if (pos_ >= text_.size()) throw std::runtime_error("unexpected end of json");
        const char c = text_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't') return parseTrue();
        if (c == 'f') return parseFalse();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        throw std::runtime_error("invalid json token");
    }

    JsonValue parseObject()
    {
        JsonValue v;
        v.type = JsonType::Object;
        ++pos_; // {
        skipWs();
        if (peek('}')) {
            ++pos_;
            return v;
        }
        while (true) {
            JsonValue key = parseString();
            skipWs();
            expect(':');
            JsonValue value = parseValue();
            v.o.emplace(key.s, std::move(value));
            skipWs();
            if (peek('}')) {
                ++pos_;
                break;
            }
            expect(',');
            skipWs();
        }
        return v;
    }

    JsonValue parseArray()
    {
        JsonValue v;
        v.type = JsonType::Array;
        ++pos_; // [
        skipWs();
        if (peek(']')) {
            ++pos_;
            return v;
        }
        while (true) {
            v.a.push_back(parseValue());
            skipWs();
            if (peek(']')) {
                ++pos_;
                break;
            }
            expect(',');
            skipWs();
        }
        return v;
    }

    JsonValue parseString()
    {
        expect('"');
        JsonValue v;
        v.type = JsonType::String;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return v;
            if (c == '\\') {
                if (pos_ >= text_.size()) throw std::runtime_error("bad json escape");
                const char e = text_[pos_++];
                switch (e) {
                    case '"': v.s.push_back('"'); break;
                    case '\\': v.s.push_back('\\'); break;
                    case '/': v.s.push_back('/'); break;
                    case 'b': v.s.push_back('\b'); break;
                    case 'f': v.s.push_back('\f'); break;
                    case 'n': v.s.push_back('\n'); break;
                    case 'r': v.s.push_back('\r'); break;
                    case 't': v.s.push_back('\t'); break;
                    case 'u': throw std::runtime_error("unicode escape not supported in minimal parser");
                    default: throw std::runtime_error("invalid json escape");
                }
            } else {
                v.s.push_back(c);
            }
        }
        throw std::runtime_error("unterminated json string");
    }

    JsonValue parseTrue()
    {
        consumeLiteral("true");
        JsonValue v;
        v.type = JsonType::Bool;
        v.b = true;
        return v;
    }

    JsonValue parseFalse()
    {
        consumeLiteral("false");
        JsonValue v;
        v.type = JsonType::Bool;
        v.b = false;
        return v;
    }

    JsonValue parseNull()
    {
        consumeLiteral("null");
        JsonValue v;
        v.type = JsonType::Null;
        return v;
    }

    JsonValue parseNumber()
    {
        const size_t start = pos_;
        if (peek('-')) ++pos_;
        if (peek('0')) {
            ++pos_;
        } else {
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("bad number");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (peek('.')) {
            ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("bad fraction");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (peek('e') || peek('E')) {
            ++pos_;
            if (peek('+') || peek('-')) ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) throw std::runtime_error("bad exponent");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        const std::string_view sv = text_.substr(start, pos_ - start);
        JsonValue v;
        v.type = JsonType::Number;
        std::string temp(sv);
        char* end = nullptr;
        v.n = std::strtod(temp.c_str(), &end);
        if (!end || *end != '\0') throw std::runtime_error("failed number parse");
        return v;
    }

    void consumeLiteral(const char* lit)
    {
        const size_t len = std::strlen(lit);
        if (text_.substr(pos_, len) != lit) throw std::runtime_error("invalid json literal");
        pos_ += len;
    }

    void skipWs()
    {
        while (pos_ < text_.size()) {
            if (!std::isspace(static_cast<unsigned char>(text_[pos_]))) break;
            ++pos_;
        }
    }

    void expect(char c)
    {
        if (pos_ >= text_.size() || text_[pos_] != c) throw std::runtime_error("unexpected json character");
        ++pos_;
    }

    bool peek(char c) const { return pos_ < text_.size() && text_[pos_] == c; }

    std::string_view text_;
    size_t pos_ = 0;
};

std::vector<uint8_t> readBinaryFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("failed to open file: " + path);
    const std::streamsize size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(data.data()), size)) throw std::runtime_error("failed to read file: " + path);
    return data;
}

std::string readTextFile(const std::string& path)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string directoryOf(const std::string& path)
{
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct AccessorView {
    const uint8_t* data = nullptr;
    size_t count = 0;
    size_t stride = 0;
    int componentType = 0;
    int componentCount = 0;
    bool normalized = false;
};

int componentCountFromType(const std::string& type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

size_t componentSize(int componentType)
{
    switch (componentType) {
        case 5120: return 1; // BYTE
        case 5121: return 1; // UBYTE
        case 5122: return 2; // SHORT
        case 5123: return 2; // USHORT
        case 5125: return 4; // UINT
        case 5126: return 4; // FLOAT
        default: return 0;
    }
}

float readComponentAsFloat(const uint8_t* p, int componentType, bool normalized)
{
    switch (componentType) {
        case 5126: {
            float v;
            std::memcpy(&v, p, sizeof(float));
            return v;
        }
        case 5121: {
            const uint8_t v = *p;
            return normalized ? static_cast<float>(v) / 255.0f : static_cast<float>(v);
        }
        case 5123: {
            uint16_t v;
            std::memcpy(&v, p, sizeof(uint16_t));
            return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
        }
        case 5125: {
            uint32_t v;
            std::memcpy(&v, p, sizeof(uint32_t));
            return static_cast<float>(v);
        }
        case 5120: {
            int8_t v;
            std::memcpy(&v, p, sizeof(int8_t));
            if (!normalized) return static_cast<float>(v);
            return std::max(-1.0f, static_cast<float>(v) / 127.0f);
        }
        case 5122: {
            int16_t v;
            std::memcpy(&v, p, sizeof(int16_t));
            if (!normalized) return static_cast<float>(v);
            return std::max(-1.0f, static_cast<float>(v) / 32767.0f);
        }
        default: return 0.0f;
    }
}

uint32_t readIndex(const uint8_t* p, int componentType)
{
    switch (componentType) {
        case 5121: {
            uint8_t v;
            std::memcpy(&v, p, sizeof(uint8_t));
            return static_cast<uint32_t>(v);
        }
        case 5123: {
            uint16_t v;
            std::memcpy(&v, p, sizeof(uint16_t));
            return static_cast<uint32_t>(v);
        }
        case 5125: {
            uint32_t v;
            std::memcpy(&v, p, sizeof(uint32_t));
            return v;
        }
        default:
            throw std::runtime_error("unsupported index component type");
    }
}

AccessorView makeAccessorView(const JsonValue& doc, const std::vector<std::vector<uint8_t>>& buffers, int accessorIndex)
{
    const JsonValue* accessors = doc.get("accessors");
    const JsonValue* bufferViews = doc.get("bufferViews");
    if (!accessors || !bufferViews || accessors->type != JsonType::Array || bufferViews->type != JsonType::Array) {
        throw std::runtime_error("gltf missing accessors or bufferViews");
    }
    const JsonValue* acc = accessors->at(static_cast<size_t>(accessorIndex));
    if (!acc || acc->type != JsonType::Object) throw std::runtime_error("invalid accessor index");

    const JsonValue* bufferViewField = acc->get("bufferView");
    if (!bufferViewField) throw std::runtime_error("accessor missing bufferView field");
    const int bufferViewIndex = static_cast<int>(bufferViewField->asInt().value_or(-1));
    if (bufferViewIndex < 0) throw std::runtime_error("accessor missing bufferView");

    const JsonValue* bv = bufferViews->at(static_cast<size_t>(bufferViewIndex));
    if (!bv || bv->type != JsonType::Object) throw std::runtime_error("invalid bufferView index");

    const JsonValue* bufferField = bv->get("buffer");
    if (!bufferField) throw std::runtime_error("bufferView missing buffer field");
    const int bufferIndex = static_cast<int>(bufferField->asInt().value_or(-1));
    if (bufferIndex < 0 || static_cast<size_t>(bufferIndex) >= buffers.size()) throw std::runtime_error("invalid buffer index");

    const JsonValue* componentField = acc->get("componentType");
    const JsonValue* typeField = acc->get("type");
    const JsonValue* countField = acc->get("count");
    if (!componentField || !typeField || !countField) throw std::runtime_error("accessor missing required fields");
    const int componentType = static_cast<int>(componentField->asInt().value_or(0));
    const std::string type = typeField->asString().value_or("");
    const int compCount = componentCountFromType(type);
    if (compCount <= 0) throw std::runtime_error("unsupported accessor type");
    const size_t compSize = componentSize(componentType);
    if (compSize == 0) throw std::runtime_error("unsupported accessor component type");

    const size_t count = static_cast<size_t>(countField->asInt().value_or(0));
    const size_t accessorOffset = static_cast<size_t>(acc->get("byteOffset") ? acc->get("byteOffset")->asInt().value_or(0) : 0);
    const size_t viewOffset = static_cast<size_t>(bv->get("byteOffset") ? bv->get("byteOffset")->asInt().value_or(0) : 0);
    const size_t stride = static_cast<size_t>(bv->get("byteStride") ? bv->get("byteStride")->asInt().value_or(0) : compSize * compCount);
    const bool normalized = acc->get("normalized") && acc->get("normalized")->type == JsonType::Bool && acc->get("normalized")->b;

    const std::vector<uint8_t>& buf = buffers[static_cast<size_t>(bufferIndex)];
    const size_t begin = viewOffset + accessorOffset;
    if (begin + count * stride > buf.size()) throw std::runtime_error("accessor points outside buffer");

    return AccessorView{
        .data = buf.data() + begin,
        .count = count,
        .stride = stride,
        .componentType = componentType,
        .componentCount = compCount,
        .normalized = normalized,
    };
}

std::vector<std::vector<uint8_t>> loadBuffersFromDocument(const JsonValue& doc, const std::string& modelPath)
{
    const JsonValue* buffers = doc.get("buffers");
    if (!buffers || buffers->type != JsonType::Array) throw std::runtime_error("gltf missing buffers array");
    std::vector<std::vector<uint8_t>> out;
    out.reserve(buffers->a.size());
    const std::string baseDir = directoryOf(modelPath);

    for (size_t i = 0; i < buffers->a.size(); ++i) {
        const JsonValue& b = buffers->a[i];
        if (b.type != JsonType::Object) throw std::runtime_error("invalid buffer entry");
        const std::string uri = b.get("uri") ? b.get("uri")->asString().value_or("") : "";
        if (uri.empty()) {
            throw std::runtime_error("external .gltf missing buffer uri for buffer " + std::to_string(i));
        }
        if (uri.rfind("data:", 0) == 0) {
            throw std::runtime_error("data URI buffers are not supported in minimal loader");
        }
        out.push_back(readBinaryFile(baseDir + "/" + uri));
    }
    return out;
}

void loadMeshFromDocument(const JsonValue& doc, const std::vector<std::vector<uint8_t>>& buffers, std::vector<core::Vertex>& outVertices,
                          std::vector<uint32_t>& outIndices)
{
    const JsonValue* meshes = doc.get("meshes");
    if (!meshes || meshes->type != JsonType::Array || meshes->a.empty()) throw std::runtime_error("gltf has no meshes");

    const JsonValue& mesh0 = meshes->a[0];
    const JsonValue* prims = mesh0.get("primitives");
    if (!prims || prims->type != JsonType::Array || prims->a.empty()) throw std::runtime_error("mesh has no primitives");

    const JsonValue& prim = prims->a[0];
    const JsonValue* attrs = prim.get("attributes");
    if (!attrs || attrs->type != JsonType::Object) throw std::runtime_error("primitive missing attributes");

    const int posAcc = static_cast<int>(attrs->get("POSITION") ? attrs->get("POSITION")->asInt().value_or(-1) : -1);
    if (posAcc < 0) throw std::runtime_error("primitive missing POSITION accessor");
    const int colorAcc = static_cast<int>(attrs->get("COLOR_0") ? attrs->get("COLOR_0")->asInt().value_or(-1) : -1);
    const int uvAcc = static_cast<int>(attrs->get("TEXCOORD_0") ? attrs->get("TEXCOORD_0")->asInt().value_or(-1) : -1);

    const AccessorView pos = makeAccessorView(doc, buffers, posAcc);
    if (pos.componentCount < 3) throw std::runtime_error("POSITION accessor must be vec3");

    std::optional<AccessorView> color;
    if (colorAcc >= 0) color = makeAccessorView(doc, buffers, colorAcc);
    std::optional<AccessorView> uv;
    if (uvAcc >= 0) uv = makeAccessorView(doc, buffers, uvAcc);

    outVertices.clear();
    outVertices.reserve(pos.count);

    glm::vec3 minP(std::numeric_limits<float>::max());
    glm::vec3 maxP(std::numeric_limits<float>::lowest());

    for (size_t i = 0; i < pos.count; ++i) {
        const uint8_t* p = pos.data + i * pos.stride;
        glm::vec3 vtxPos(
            readComponentAsFloat(p + 0 * componentSize(pos.componentType), pos.componentType, pos.normalized),
            readComponentAsFloat(p + 1 * componentSize(pos.componentType), pos.componentType, pos.normalized),
            readComponentAsFloat(p + 2 * componentSize(pos.componentType), pos.componentType, pos.normalized));

        glm::vec3 col(1.0f, 1.0f, 1.0f);
        if (color.has_value() && i < color->count) {
            const uint8_t* c = color->data + i * color->stride;
            const size_t csz = componentSize(color->componentType);
            col.r = readComponentAsFloat(c + 0 * csz, color->componentType, color->normalized);
            col.g = (color->componentCount > 1) ? readComponentAsFloat(c + 1 * csz, color->componentType, color->normalized) : col.r;
            col.b = (color->componentCount > 2) ? readComponentAsFloat(c + 2 * csz, color->componentType, color->normalized) : col.r;
        }

        glm::vec2 tex(0.0f, 0.0f);
        if (uv.has_value() && i < uv->count) {
            const uint8_t* t = uv->data + i * uv->stride;
            const size_t tsz = componentSize(uv->componentType);
            tex.x = readComponentAsFloat(t + 0 * tsz, uv->componentType, uv->normalized);
            tex.y = (uv->componentCount > 1) ? readComponentAsFloat(t + 1 * tsz, uv->componentType, uv->normalized) : 0.0f;
        }

        minP = glm::min(minP, vtxPos);
        maxP = glm::max(maxP, vtxPos);
        outVertices.push_back(core::Vertex{vtxPos, col, tex});
    }

    const glm::vec3 extent = maxP - minP;
    const float maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (maxExtent > 0.0f) {
        const glm::vec3 center = (maxP + minP) * 0.5f;
        const float scale = 80.0f / maxExtent;
        for (auto& v : outVertices) {
            v.pos = (v.pos - center) * scale;
        }
    }

    outIndices.clear();
    const int idxAcc = static_cast<int>(prim.get("indices") ? prim.get("indices")->asInt().value_or(-1) : -1);
    if (idxAcc >= 0) {
        const AccessorView idx = makeAccessorView(doc, buffers, idxAcc);
        if (idx.componentCount != 1) throw std::runtime_error("index accessor must be scalar");
        outIndices.reserve(idx.count);
        for (size_t i = 0; i < idx.count; ++i) {
            const uint8_t* p = idx.data + i * idx.stride;
            outIndices.push_back(readIndex(p, idx.componentType));
        }
    } else {
        outIndices.reserve(outVertices.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(outVertices.size()); ++i) outIndices.push_back(i);
    }
}

void loadFromGlb(const std::string& path, std::vector<core::Vertex>& outVertices, std::vector<uint32_t>& outIndices)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);
    if (bytes.size() < 20) throw std::runtime_error("glb file too small");

    const uint32_t magic = readU32LE(bytes.data() + 0);
    const uint32_t version = readU32LE(bytes.data() + 4);
    const uint32_t length = readU32LE(bytes.data() + 8);
    if (magic != 0x46546C67) throw std::runtime_error("invalid glb magic");
    if (version != 2) throw std::runtime_error("unsupported glb version");
    if (length > bytes.size()) throw std::runtime_error("invalid glb length");

    std::string jsonText;
    std::vector<std::vector<uint8_t>> buffers;

    size_t offset = 12;
    while (offset + 8 <= length) {
        const uint32_t chunkLen = readU32LE(bytes.data() + offset);
        const uint32_t chunkType = readU32LE(bytes.data() + offset + 4);
        offset += 8;
        if (offset + chunkLen > length) throw std::runtime_error("corrupt glb chunk");

        if (chunkType == 0x4E4F534A) {
            jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), chunkLen);
            while (!jsonText.empty() && (jsonText.back() == '\0' || std::isspace(static_cast<unsigned char>(jsonText.back())))) jsonText.pop_back();
        } else if (chunkType == 0x004E4942) {
            buffers.push_back(std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLen)));
        }
        offset += chunkLen;
    }

    if (jsonText.empty()) throw std::runtime_error("glb missing JSON chunk");
    if (buffers.empty()) buffers.emplace_back();

    const JsonValue doc = JsonParser(jsonText).parse();
    loadMeshFromDocument(doc, buffers, outVertices, outIndices);
}

void loadFromGltf(const std::string& path, std::vector<core::Vertex>& outVertices, std::vector<uint32_t>& outIndices)
{
    const std::string jsonText = readTextFile(path);
    const JsonValue doc = JsonParser(jsonText).parse();
    const std::vector<std::vector<uint8_t>> buffers = loadBuffersFromDocument(doc, path);
    loadMeshFromDocument(doc, buffers, outVertices, outIndices);
}

} // namespace

GltfModelObject::GltfModelObject(std::string path, uint32_t textureSlot)
    : RenderObject("GltfModelObject", PrimitiveType::Triangles, ShaderSet{"cube.vert.spv", "cube.frag.spv"},
                   Material{.textureSlot = textureSlot, .baseColor = glm::vec4(1.0f)}),
      path_(std::move(path))
{
    try {
        if (endsWith(path_, ".glb")) {
            loadFromGlb(path_, vertices_, indices_);
        } else if (endsWith(path_, ".gltf")) {
            loadFromGltf(path_, vertices_, indices_);
        } else {
            throw std::runtime_error("model extension must be .gltf or .glb");
        }
        loaded_ = !vertices_.empty() && !indices_.empty();
        if (!loaded_) {
            error_ = "loaded model has no vertices/indices";
        } else {
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(70.0f, 0.0f, 0.0f));
            setModelMatrix(model);
        }
    } catch (const std::exception& e) {
        loaded_ = false;
        error_ = e.what();
    }
}

void GltfModelObject::buildMesh(std::vector<core::Vertex>& outVertices, std::vector<uint32_t>& outIndices) const
{
    outVertices = vertices_;
    outIndices = indices_;
}

void GltfModelObject::update(float /*deltaSeconds*/, float elapsedSeconds)
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, baseTranslation_);
    model = glm::rotate(model, elapsedSeconds * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, elapsedSeconds * 0.35f, glm::vec3(1.0f, 0.0f, 0.0f));
    setModelMatrix(model);
}

} // namespace vkscene
