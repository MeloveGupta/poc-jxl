// JPEG XL decode proof-of-concept — eventual home: vcl/source/filter/jxl/reader.cxx
// Build: g++ -std=c++17 -o jxl_poc jxl_poc.cpp -ljxl
// Usage: ./jxl_poc input.jxl [output.ppm]

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>

// The two magic signatures for JXL files. The bare codestream one is just two
// bytes; the container format uses the standard ISOBMFF box header.
static const uint8_t kBareCodestreamSig[] = {0xFF, 0x0A};
static const uint8_t kContainerSig[] = {
    0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A};

static std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        std::cerr << "couldn't open file: " << path << "\n";
        return {};
    }

    auto size = f.tellg();
    if (size <= 0)
    {
        std::cerr << "file is empty or unreadable: " << path << "\n";
        return {};
    }

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// Quick manual check before we hand things off to libjxl.
// Returns a human-readable label for what we detected, or empty string on fail.
static std::string detectMagic(const std::vector<uint8_t>& data)
{
    if (data.size() >= sizeof(kContainerSig)
        && std::memcmp(data.data(), kContainerSig, sizeof(kContainerSig)) == 0)
    {
        return "ISOBMFF container";
    }
    if (data.size() >= sizeof(kBareCodestreamSig)
        && std::memcmp(data.data(), kBareCodestreamSig, sizeof(kBareCodestreamSig)) == 0)
    {
        return "bare codestream";
    }
    return "";
}

static bool writePPM(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        std::cerr << "can't write ppm: " << path << "\n";
        return false;
    }

    // P6 is RGB-only, so we strip the alpha channel here
    f << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t i = 0; i < w * h; ++i)
    {
        f.write(reinterpret_cast<const char*>(&rgba[i * 4]), 3);
    }
    return true;
}

// The main decode loop. This mirrors how the LibreOffice VCL filter will work:
// feed all data at once, then pump the event loop until JXL_DEC_SUCCESS.
static bool decodeJXL(const std::vector<uint8_t>& data, std::vector<uint8_t>& pixels,
                      uint32_t& width, uint32_t& height)
{
    auto dec = JxlDecoderMake(nullptr);
    if (!dec)
    {
        std::cerr << "failed to create decoder\n";
        return false;
    }

    auto status = JxlDecoderSubscribeEvents(
        dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);
    if (status != JXL_DEC_SUCCESS)
    {
        std::cerr << "couldn't subscribe to decoder events\n";
        return false;
    }

    JxlDecoderSetInput(dec.get(), data.data(), data.size());
    JxlDecoderCloseInput(dec.get());

    JxlBasicInfo info{};
    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    for (;;)
    {
        status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_BASIC_INFO)
        {
            if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS)
            {
                std::cerr << "couldn't read basic info\n";
                return false;
            }

            width = info.xsize;
            height = info.ysize;

            std::cout << "--- image info ---\n"
                      << "  dimensions:  " << info.xsize << " x " << info.ysize << "\n"
                      << "  bit depth:   " << info.bits_per_sample << "\n"
                      << "  channels:    " << info.num_color_channels << "\n"
                      << "  has alpha:   " << (info.alpha_bits > 0 ? "yes" : "no") << "\n"
                      << "  animated:    " << (info.have_animation ? "yes" : "no") << "\n";
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
            size_t bufSize;
            if (JxlDecoderImageOutBufferSize(dec.get(), &format, &bufSize)
                != JXL_DEC_SUCCESS)
            {
                std::cerr << "couldn't determine output buffer size\n";
                return false;
            }

            pixels.resize(bufSize);
            if (JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels.data(), bufSize)
                != JXL_DEC_SUCCESS)
            {
                std::cerr << "couldn't set output buffer\n";
                return false;
            }
        }
        else if (status == JXL_DEC_FULL_IMAGE)
        {
            // pixels are ready — nothing to do, loop will hit SUCCESS next
        }
        else if (status == JXL_DEC_SUCCESS)
        {
            break;
        }
        else
        {
            // shouldn't happen if the file is valid, but just in case
            std::cerr << "decoder error, status=" << static_cast<int>(status) << "\n";
            return false;
        }
    }

    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " input.jxl [output.ppm]\n";
        return 1;
    }

    auto data = readFile(argv[1]);
    if (data.empty())
        return 1;

    // manual signature check
    auto sigType = detectMagic(data);
    if (!sigType.empty())
    {
        std::cout << "manual magic detect: " << sigType << "\n";
    }
    else
    {
        std::cout << "manual magic detect: no match\n";
    }

    // libjxl's own check — this handles edge cases and partial codestreams
    // that our simple memcmp above would miss
    auto sig = JxlSignatureCheck(data.data(), data.size());
    if (sig == JXL_SIG_NOT_ENOUGH_BYTES || sig == JXL_SIG_INVALID)
    {
        std::cerr << "not a valid JXL file (JxlSignatureCheck failed)\n";
        return 1;
    }
    std::cout << "JxlSignatureCheck: valid (type="
              << (sig == JXL_SIG_CODESTREAM ? "codestream" : "container") << ")\n\n";

    uint32_t w = 0, h = 0;
    std::vector<uint8_t> pixels;

    if (!decodeJXL(data, pixels, w, h))
    {
        std::cerr << "decode failed\n";
        return 1;
    }

    std::cout << "\ndecoded " << w << "x" << h << " (" << pixels.size() << " bytes)\n";

    if (argc >= 3)
    {
        if (writePPM(argv[2], pixels.data(), w, h))
            std::cout << "wrote " << argv[2] << "\n";
    }

    return 0;
}
