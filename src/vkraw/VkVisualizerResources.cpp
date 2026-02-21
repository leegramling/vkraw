#include "vkraw/VkVisualizerApp.h"

#include "vkraw/CubeRenderTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#if defined(VKRAW_ENABLE_IMAGE_FILE_IO)
#include <jpeglib.h>
#include <png.h>
#include <tiffio.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace vkraw {

namespace {

struct LoadedImage
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels{};
};

LoadedImage resizeRgbaNearest(const LoadedImage& src, uint32_t dstWidth, uint32_t dstHeight)
{
    LoadedImage dst{};
    dst.width = std::max(1U, dstWidth);
    dst.height = std::max(1U, dstHeight);
    dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height * 4U);
    for (uint32_t y = 0; y < dst.height; ++y) {
        const uint32_t sy = (static_cast<uint64_t>(y) * src.height) / dst.height;
        for (uint32_t x = 0; x < dst.width; ++x) {
            const uint32_t sx = (static_cast<uint64_t>(x) * src.width) / dst.width;
            const size_t srcIndex = (static_cast<size_t>(sy) * src.width + sx) * 4U;
            const size_t dstIndex = (static_cast<size_t>(y) * dst.width + x) * 4U;
            dst.pixels[dstIndex + 0] = src.pixels[srcIndex + 0];
            dst.pixels[dstIndex + 1] = src.pixels[srcIndex + 1];
            dst.pixels[dstIndex + 2] = src.pixels[srcIndex + 2];
            dst.pixels[dstIndex + 3] = src.pixels[srcIndex + 3];
        }
    }
    return dst;
}

std::vector<uint8_t> readBinaryFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) return {};
    const std::streamsize size = input.tellg();
    if (size <= 0) return {};
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

#if defined(VKRAW_ENABLE_IMAGE_FILE_IO)
bool decodeJpeg(const std::string& path, LoadedImage& out)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);
    if (bytes.empty()) return false;

    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr jerr{};
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, bytes.data(), bytes.size());
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    out.width = cinfo.output_width;
    out.height = cinfo.output_height;
    const uint32_t channels = cinfo.output_components;
    if (out.width == 0 || out.height == 0 || channels < 3) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    std::vector<uint8_t> rgb(static_cast<size_t>(out.width) * out.height * channels);
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row = rgb.data() + static_cast<size_t>(cinfo.output_scanline) * out.width * channels;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4U);
    for (size_t i = 0, j = 0; i < out.pixels.size(); i += 4, j += channels) {
        out.pixels[i + 0] = rgb[j + 0];
        out.pixels[i + 1] = rgb[j + 1];
        out.pixels[i + 2] = rgb[j + 2];
        out.pixels[i + 3] = 255;
    }
    return true;
}

bool decodePng(const std::string& path, LoadedImage& out)
{
    const std::vector<uint8_t> bytes = readBinaryFile(path);
    if (bytes.size() < 8 || png_sig_cmp(bytes.data(), 0, 8) != 0) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return false;
    }

    struct PngBufferReader {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t offset = 0;
    } reader{bytes.data(), bytes.size(), 0};

    png_set_read_fn(
        png,
        &reader,
        [](png_structp pngPtr, png_bytep outBytes, png_size_t byteCount) {
            auto* r = reinterpret_cast<PngBufferReader*>(png_get_io_ptr(pngPtr));
            if (!r || (r->offset + byteCount) > r->size) png_error(pngPtr, "png read overflow");
            std::memcpy(outBytes, r->data + r->offset, byteCount);
            r->offset += byteCount;
        });

    png_read_info(png, info);
    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const int colorType = png_get_color_type(png, info);
    const int bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if ((colorType & PNG_COLOR_MASK_ALPHA) == 0) png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);
    out.width = static_cast<uint32_t>(width);
    out.height = static_cast<uint32_t>(height);
    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4U);

    std::vector<png_bytep> rows(out.height);
    for (uint32_t y = 0; y < out.height; ++y) {
        rows[y] = out.pixels.data() + static_cast<size_t>(y) * out.width * 4U;
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    return true;
}

bool decodeTiff(const std::string& path, LoadedImage& out)
{
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) return false;

    uint32_t width = 0;
    uint32_t height = 0;
    if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) != 1 || TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) != 1 || width == 0 ||
        height == 0) {
        TIFFClose(tif);
        return false;
    }

    std::vector<uint32_t> rgba(static_cast<size_t>(width) * height);
    const int ok = TIFFReadRGBAImageOriented(tif, width, height, rgba.data(), ORIENTATION_TOPLEFT, 0);
    TIFFClose(tif);
    if (ok != 1) return false;

    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height * 4U);
    for (size_t i = 0; i < rgba.size(); ++i) {
        const uint32_t pixel = rgba[i];
        out.pixels[i * 4U + 0] = TIFFGetR(pixel);
        out.pixels[i * 4U + 1] = TIFFGetG(pixel);
        out.pixels[i * 4U + 2] = TIFFGetB(pixel);
        out.pixels[i * 4U + 3] = TIFFGetA(pixel);
    }
    return true;
}
#endif

bool loadTextureFromFile(const std::string& path, LoadedImage& out)
{
    if (path.empty()) return false;
#if !defined(VKRAW_ENABLE_IMAGE_FILE_IO)
    (void)out;
    return false;
#else
    const std::string ext = std::filesystem::path(path).extension().string();
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lowerExt == ".jpg" || lowerExt == ".jpeg") return decodeJpeg(path, out);
    if (lowerExt == ".png") return decodePng(path, out);
    if (lowerExt == ".tif" || lowerExt == ".tiff") return decodeTiff(path, out);
    return false;
#endif
}

std::vector<uint8_t> makeProceduralEarthTexture(uint32_t width, uint32_t height)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4U, 255U);
    for (uint32_t y = 0; y < height; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(std::max(1U, height - 1U));
        const float lat = (0.5f - v) * glm::pi<float>();
        const float sinLat = std::sin(lat);
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max(1U, width - 1U));
            const float lon = (u * 2.0f - 1.0f) * glm::pi<float>();

            const float noiseA = std::sin(lon * 2.7f + 0.4f) * std::cos(lat * 3.3f - 0.1f);
            const float noiseB = std::sin(lon * 8.4f - lat * 2.9f);
            const float elevation = 0.62f * noiseA + 0.38f * noiseB;
            const bool polar = std::abs(lat) > glm::radians(70.0f);
            const bool land = (elevation + 0.25f * sinLat) > 0.08f;

            glm::vec3 color = land ? glm::vec3(0.23f, 0.50f, 0.20f) : glm::vec3(0.06f, 0.18f, 0.45f);
            if (land) {
                color = glm::mix(color, glm::vec3(0.62f, 0.53f, 0.33f), std::clamp((elevation - 0.10f) * 0.9f, 0.0f, 1.0f));
            }
            if (polar) {
                color = glm::mix(color, glm::vec3(0.92f, 0.95f, 0.98f), 0.85f);
            }

            const size_t index = (static_cast<size_t>(y) * width + x) * 4U;
            pixels[index + 0] = static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[index + 1] = static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[index + 2] = static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

uint32_t VkVisualizerApp::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(context_.physicalDevice.physical_device, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1U << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type");
}

void VkVisualizerApp::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                  VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context_.device.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(context_.device.device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context_.device.device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(context_.device.device, buffer, bufferMemory, 0);
}

void VkVisualizerApp::uploadToMemory(VkDeviceMemory memory, const void* src, VkDeviceSize size) {
    void* data = nullptr;
    vkMapMemory(context_.device.device, memory, 0, size, 0, &data);
    std::memcpy(data, src, static_cast<size_t>(size));
    vkUnmapMemory(context_.device.device, memory);
}

void VkVisualizerApp::createVertexBuffer() {
    const VkDeviceSize bufferSize = sizeof(Vertex) * sceneVertices_.size();
    if (bufferSize == 0) {
        throw std::runtime_error("scene vertex buffer is empty");
    }
    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.vertexBuffer, context_.vertexBufferMemory);
    uploadToMemory(context_.vertexBufferMemory, sceneVertices_.data(), bufferSize);
}

void VkVisualizerApp::createIndexBuffer() {
    const VkDeviceSize bufferSize = sizeof(uint32_t) * sceneIndices_.size();
    if (bufferSize == 0) {
        throw std::runtime_error("scene index buffer is empty");
    }
    createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.indexBuffer, context_.indexBufferMemory);
    uploadToMemory(context_.indexBufferMemory, sceneIndices_.data(), bufferSize);
    sceneIndexCount_ = static_cast<uint32_t>(sceneIndices_.size());
}

void VkVisualizerApp::createUniformBuffer() {
    createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, context_.uniformBuffer, context_.uniformBufferMemory);
}

void VkVisualizerApp::createTextureResources() {
    LoadedImage image{};
    textureLoadedFromFile_ = loadTextureFromFile(earthTexturePath_, image);
    textureSourceLabel_ = "procedural";
    if (!textureLoadedFromFile_) {
        if (!earthTexturePath_.empty()) {
#if !defined(VKRAW_ENABLE_IMAGE_FILE_IO)
            std::cerr << "Earth file texture loading is disabled in this build (missing JPEG/PNG/TIFF libs), using procedural fallback texture.\n";
#else
            std::cerr << "Failed to load earth texture at '" << earthTexturePath_
                      << "' (supported formats: .jpg/.jpeg/.png/.tif/.tiff), using procedural fallback texture.\n";
#endif
        }
        constexpr uint32_t kTextureWidth = 1024;
        constexpr uint32_t kTextureHeight = 512;
        image.width = kTextureWidth;
        image.height = kTextureHeight;
        image.pixels = makeProceduralEarthTexture(kTextureWidth, kTextureHeight);
    } else {
        textureSourceLabel_ = "file";
        if (!earthTexturePath_.empty()) {
            textureSourceLabel_ += ":" + earthTexturePath_;
        }
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(context_.physicalDevice.physical_device, &props);
    if (image.width > props.limits.maxImageDimension2D || image.height > props.limits.maxImageDimension2D) {
        const float scaleW = static_cast<float>(props.limits.maxImageDimension2D) / static_cast<float>(image.width);
        const float scaleH = static_cast<float>(props.limits.maxImageDimension2D) / static_cast<float>(image.height);
        const float scale = std::min(scaleW, scaleH);
        const uint32_t newWidth = std::max(1U, static_cast<uint32_t>(std::floor(image.width * scale)));
        const uint32_t newHeight = std::max(1U, static_cast<uint32_t>(std::floor(image.height * scale)));
        std::cerr << "Earth texture '" << (earthTexturePath_.empty() ? "<procedural>" : earthTexturePath_) << "' exceeds GPU max dimension "
                  << props.limits.maxImageDimension2D << ", downscaling " << image.width << "x" << image.height << " -> " << newWidth << "x"
                  << newHeight << ".\n";
        image = resizeRgbaNearest(image, newWidth, newHeight);
        if (textureLoadedFromFile_) {
            textureSourceLabel_ += "(downscaled)";
        } else {
            textureSourceLabel_ = "procedural(downscaled)";
        }
    }
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(image.pixels.size());

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);
    uploadToMemory(stagingMemory, image.pixels.data(), imageSize);

    createImage(image.width, image.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                context_.earthTextureImage, context_.earthTextureMemory);

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier toTransferBarrier{};
    toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.image = context_.earthTextureImage;
    toTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferBarrier.subresourceRange.baseMipLevel = 0;
    toTransferBarrier.subresourceRange.levelCount = 1;
    toTransferBarrier.subresourceRange.baseArrayLayer = 0;
    toTransferBarrier.subresourceRange.layerCount = 1;
    toTransferBarrier.srcAccessMask = 0;
    toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransferBarrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, context_.earthTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShaderReadBarrier{};
    toShaderReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderReadBarrier.image = context_.earthTextureImage;
    toShaderReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toShaderReadBarrier.subresourceRange.baseMipLevel = 0;
    toShaderReadBarrier.subresourceRange.levelCount = 1;
    toShaderReadBarrier.subresourceRange.baseArrayLayer = 0;
    toShaderReadBarrier.subresourceRange.layerCount = 1;
    toShaderReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toShaderReadBarrier);

    endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(context_.device.device, stagingBuffer, nullptr);
    vkFreeMemory(context_.device.device, stagingMemory, nullptr);

    context_.earthTextureView = createImageView(context_.earthTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 8.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(context_.physicalDevice.physical_device, &supportedFeatures);
    if (supportedFeatures.samplerAnisotropy != VK_TRUE) {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }

    if (vkCreateSampler(context_.device.device, &samplerInfo, nullptr, &context_.earthTextureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create earth texture sampler");
    }
}

void VkVisualizerApp::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(context_.device.device, &poolInfo, nullptr, &context_.descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }
}

void VkVisualizerApp::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context_.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &context_.descriptorSetLayout;

    if (vkAllocateDescriptorSets(context_.device.device, &allocInfo, &context_.descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = context_.uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = context_.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.pBufferInfo = &bufferInfo;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = context_.earthTextureView;
    imageInfo.sampler = context_.earthTextureSampler;

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = context_.descriptorSet;
    textureWrite.dstBinding = 1;
    textureWrite.descriptorCount = 1;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = &imageInfo;

    const std::array<VkWriteDescriptorSet, 2> writes{descriptorWrite, textureWrite};
    vkUpdateDescriptorSets(context_.device.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VkVisualizerApp::createCommandBuffers() {
    context_.commandBuffers.resize(context_.swapchainImages.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context_.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(context_.commandBuffers.size());

    if (vkAllocateCommandBuffers(context_.device.device, &allocInfo, context_.commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void VkVisualizerApp::createTimestampQueryPool() {
    if (!context_.gpuTimestampsSupported) {
        return;
    }

    if (context_.gpuTimestampQueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(context_.device.device, context_.gpuTimestampQueryPool, nullptr);
        context_.gpuTimestampQueryPool = VK_NULL_HANDLE;
    }

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 2U * static_cast<uint32_t>(kMaxFramesInFlight);

    if (vkCreateQueryPool(context_.device.device, &queryPoolInfo, nullptr, &context_.gpuTimestampQueryPool) != VK_SUCCESS) {
        context_.gpuTimestampsSupported = false;
        return;
    }

    context_.gpuQueryValid.fill(false);
}

void VkVisualizerApp::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateSemaphore(context_.device.device, &semaphoreInfo, nullptr, &context_.imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_.device.device, &semaphoreInfo, nullptr, &context_.renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(context_.device.device, &fenceInfo, nullptr, &context_.inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sync objects");
        }
    }
}

VkFormat VkVisualizerApp::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(context_.physicalDevice.physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported format");
}

VkFormat VkVisualizerApp::findDepthFormat() {
    context_.depthFormat = findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                       VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    return context_.depthFormat;
}

void VkVisualizerApp::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(context_.device.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image");
    }

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(context_.device.device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(context_.device.device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory");
    }

    vkBindImageMemory(context_.device.device, image, imageMemory, 0);
}

VkImageView VkVisualizerApp::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(context_.device.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view");
    }
    return imageView;
}

void VkVisualizerApp::createDepthResources() {
    createImage(context_.swapchain.extent.width, context_.swapchain.extent.height, findDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, context_.depthImage, context_.depthImageMemory);
    context_.depthImageView = createImageView(context_.depthImage, context_.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

VkCommandBuffer VkVisualizerApp::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context_.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(context_.device.device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate temporary command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin temporary command buffer");
    }
    return commandBuffer;
}

void VkVisualizerApp::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record temporary command buffer");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(context_.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit temporary command buffer");
    }
    vkQueueWaitIdle(context_.graphicsQueue);
    vkFreeCommandBuffers(context_.device.device, context_.commandPool, 1, &commandBuffer);
}

void VkVisualizerApp::transitionImageLayout(VkImage image, VkFormat, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("unsupported image layout transition");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(commandBuffer);
}

void VkVisualizerApp::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
}

void VkVisualizerApp::destroyTextureResources() {
    if (context_.earthTextureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(context_.device.device, context_.earthTextureSampler, nullptr);
        context_.earthTextureSampler = VK_NULL_HANDLE;
    }
    if (context_.earthTextureView != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device.device, context_.earthTextureView, nullptr);
        context_.earthTextureView = VK_NULL_HANDLE;
    }
    if (context_.earthTextureImage != VK_NULL_HANDLE) {
        vkDestroyImage(context_.device.device, context_.earthTextureImage, nullptr);
        context_.earthTextureImage = VK_NULL_HANDLE;
    }
    if (context_.earthTextureMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device.device, context_.earthTextureMemory, nullptr);
        context_.earthTextureMemory = VK_NULL_HANDLE;
    }
}

void VkVisualizerApp::initSceneSystems()
{
    globeEntity_ = ecs_.createEntity();
    ecs_.setTransform(globeEntity_, TransformComponent{glm::mat4(1.0f)});
    ecs_.setVisibility(globeEntity_, VisibilityComponent{true});
    ecs_.setMesh(globeEntity_, MeshComponent{});

    globeSceneNode_ = sceneGraph_.createNode("EarthGlobe", sceneGraph_.root(), globeEntity_);
    if (auto* node = sceneGraph_.find(globeSceneNode_))
    {
        node->localTransform = glm::mat4(1.0f);
        node->visible = true;
    }
    sceneGraph_.updateWorldTransforms();
}

void VkVisualizerApp::rebuildSceneMesh() {
    sceneVertices_.clear();
    sceneIndices_.clear();

    sceneGraph_.updateWorldTransforms();
    const SceneNode* globeNode = sceneGraph_.find(globeSceneNode_);
    VisibilityComponent* vis = ecs_.visibility(globeEntity_);
    if (!globeNode || !globeNode->visible || !vis || !vis->visible) {
        sceneIndexCount_ = 0;
        if (auto* mesh = ecs_.mesh(globeEntity_)) {
            mesh->vertexCount = 0;
            mesh->indexCount = 0;
        }
        return;
    }

    globe_.rebuildMesh(sceneVertices_, sceneIndices_);
    sceneIndexCount_ = static_cast<uint32_t>(sceneIndices_.size());
    if (auto* mesh = ecs_.mesh(globeEntity_)) {
        mesh->vertexCount = static_cast<uint32_t>(sceneVertices_.size());
        mesh->indexCount = sceneIndexCount_;
    }
}

void VkVisualizerApp::rebuildGpuMeshBuffers() {
    vkDeviceWaitIdle(context_.device.device);

    if (context_.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_.device.device, context_.vertexBuffer, nullptr);
        context_.vertexBuffer = VK_NULL_HANDLE;
    }
    if (context_.vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device.device, context_.vertexBufferMemory, nullptr);
        context_.vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (context_.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_.device.device, context_.indexBuffer, nullptr);
        context_.indexBuffer = VK_NULL_HANDLE;
    }
    if (context_.indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context_.device.device, context_.indexBufferMemory, nullptr);
        context_.indexBufferMemory = VK_NULL_HANDLE;
    }

    rebuildSceneMesh();
    createVertexBuffer();
    createIndexBuffer();
}

} // namespace vkraw
