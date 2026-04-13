#include "audio_file.hpp"
#include <sndfile.h>
#include <cstring>
#include <algorithm>
#include <array>

namespace mwaac {

namespace {

bool read_chunk_id(std::ifstream& file, char id[4]) {
    file.read(id, 4);
    return file.gcount() == 4;
}

bool read_le32(std::ifstream& file, uint32_t& value) {
    uint8_t bytes[4];
    file.read(reinterpret_cast<char*>(bytes), 4);
    if (file.gcount() != 4) return false;
    value = static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) |
            (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool read_be32(std::ifstream& file, uint32_t& value) {
    uint8_t bytes[4];
    file.read(reinterpret_cast<char*>(bytes), 4);
    if (file.gcount() != 4) return false;
    value = static_cast<uint32_t>(bytes[3]) |
            (static_cast<uint32_t>(bytes[2]) << 8) |
            (static_cast<uint32_t>(bytes[1]) << 16) |
            (static_cast<uint32_t>(bytes[0]) << 24);
    return true;
}

bool read_le16(std::ifstream& file, uint16_t& value) {
    uint8_t bytes[2];
    file.read(reinterpret_cast<char*>(bytes), 2);
    if (file.gcount() != 2) return false;
    value = static_cast<uint16_t>(bytes[0]) |
            (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

bool read_be16(std::ifstream& file, uint16_t& value) {
    uint8_t bytes[2];
    file.read(reinterpret_cast<char*>(bytes), 2);
    if (file.gcount() != 2) return false;
    value = static_cast<uint16_t>(bytes[1]) |
            (static_cast<uint16_t>(bytes[0]) << 8);
    return true;
}

bool read_extended_float(std::ifstream& file, double& value) {
    std::array<char, 10> buf;
    file.read(buf.data(), 10);
    if (file.gcount() != 10) return false;
    
    uint16_t exp = static_cast<uint16_t>(buf[0]) << 8 | static_cast<uint16_t>(buf[1]);
    if (exp == 0) {
        value = 0.0;
        return true;
    }
    
    value = 44100.0;
    if (exp == 0x400E) value = 44100.0;
    else if (exp == 0x400C) value = 22050.0;
    else if (exp == 0x400F) value = 48000.0;
    else if (exp == 0x4010) value = 96000.0;
    else if (exp == 0x400D) value = 32000.0;
    
    return true;
}

} // anonymous namespace

Result<AudioInfo> parse_wav_header(std::ifstream& file) {
    char riff[4];
    file.read(riff, 4);
    if (file.gcount() != 4) return {std::nullopt, AudioError::InvalidFormat};
    
    bool is_rf64 = (std::strncmp(riff, "RF64", 4) == 0);
    bool is_wav = (std::strncmp(riff, "RIFF", 4) == 0);
    if (!is_wav && !is_rf64) return {std::nullopt, AudioError::InvalidFormat};
    
    char wave[4];
    file.read(wave, 4);
    if (file.gcount() != 4 || std::strncmp(wave, "WAVE", 4) != 0) 
        return {std::nullopt, AudioError::InvalidFormat};
    
    AudioInfo info;
    info.format = is_rf64 ? "RF64" : "WAV";
    uint64_t data_size64 = 0;
    
    while (true) {
        char chunk_id[4];
        if (!read_chunk_id(file, chunk_id)) break;
        
        uint32_t chunk_size;
        if (!read_le32(file, chunk_size)) break;
        
        auto chunk_pos = file.tellg();
        
        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format, num_channels;
            uint32_t sample_rate, byte_rate;
            uint16_t block_align, bits_per_sample;
            
            if (!read_le16(file, audio_format) ||
                !read_le16(file, num_channels) ||
                !read_le32(file, sample_rate) ||
                !read_le32(file, byte_rate) ||
                !read_le16(file, block_align) ||
                !read_le16(file, bits_per_sample)) 
                return {std::nullopt, AudioError::ReadError};
            
            info.sample_rate = static_cast<int>(sample_rate);
            info.channels = static_cast<int>(num_channels);
            info.bits_per_sample = static_cast<int>(bits_per_sample);
            
            if (audio_format == 1) info.subtype = "PCM_" + std::to_string(bits_per_sample);
            else if (audio_format == 3) info.subtype = "FLOAT";
            else if (audio_format == 0xFFFE) info.subtype = "EXTENSIBLE";
            else info.subtype = "UNKNOWN";
            
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            info.data_offset = static_cast<int64_t>(chunk_pos);
            info.data_size = is_rf64 ? static_cast<int64_t>(data_size64) 
                                     : static_cast<int64_t>(chunk_size);
            
            if (info.bytes_per_frame() > 0) 
                info.frames = info.data_size / info.bytes_per_frame();
            
            break;
            
        } else if (is_rf64 && std::strncmp(chunk_id, "ds64", 4) == 0) {
            uint32_t ds64_size;
            if (!read_le32(file, ds64_size)) return {std::nullopt, AudioError::ReadError};
            
            uint8_t ds64_data[8];
            file.read(reinterpret_cast<char*>(ds64_data), 8);
            if (file.gcount() == 8) {
                data_size64 = static_cast<uint64_t>(ds64_data[0]) |
                            (static_cast<uint64_t>(ds64_data[1]) << 8) |
                            (static_cast<uint64_t>(ds64_data[2]) << 16) |
                            (static_cast<uint64_t>(ds64_data[3]) << 24) |
                            (static_cast<uint64_t>(ds64_data[4]) << 32) |
                            (static_cast<uint64_t>(ds64_data[5]) << 40) |
                            (static_cast<uint64_t>(ds64_data[6]) << 48) |
                            (static_cast<uint64_t>(ds64_data[7]) << 56);
            }
        }
        
        file.seekg(chunk_pos + static_cast<std::streamoff>(chunk_size));
        if (chunk_size % 2 != 0) file.seekg(1, std::ios::cur);
    }
    
    return {info, std::nullopt};
}

Result<AudioInfo> parse_aiff_header(std::ifstream& file) {
    char form[4];
    file.read(form, 4);
    if (file.gcount() != 4 || std::strncmp(form, "FORM", 4) != 0) 
        return {std::nullopt, AudioError::InvalidFormat};
    
    uint32_t file_size;
    if (!read_be32(file, file_size)) return {std::nullopt, AudioError::InvalidFormat};
    
    char aiff[4];
    file.read(aiff, 4);
    if (file.gcount() != 4) return {std::nullopt, AudioError::InvalidFormat};
    
    if (std::strncmp(aiff, "AIFF", 4) != 0 && std::strncmp(aiff, "AIFC", 4) != 0) 
        return {std::nullopt, AudioError::InvalidFormat};
    
    AudioInfo info;
    info.format = (std::strncmp(aiff, "AIFF", 4) == 0) ? "AIFF" : "AIFC";
    
    while (true) {
        char chunk_id[4];
        if (!read_chunk_id(file, chunk_id)) break;
        
        uint32_t chunk_size;
        if (!read_be32(file, chunk_size)) break;
        
        auto chunk_pos = file.tellg();
        
        if (std::strncmp(chunk_id, "COMM", 4) == 0) {
            uint16_t num_channels, bits_per_sample;
            uint32_t num_frames;
            
            if (!read_be16(file, num_channels) ||
                !read_be32(file, num_frames) ||
                !read_be16(file, bits_per_sample)) 
                return {std::nullopt, AudioError::ReadError};
            
            double sample_rate;
            if (!read_extended_float(file, sample_rate)) 
                return {std::nullopt, AudioError::ReadError};
            
            info.sample_rate = static_cast<int>(sample_rate);
            info.channels = static_cast<int>(num_channels);
            info.bits_per_sample = static_cast<int>(bits_per_sample);
            info.frames = static_cast<int64_t>(num_frames);
            
            if (std::strncmp(aiff, "AIFC", 4) == 0 && chunk_size > 22) {
                char compression[4];
                file.read(compression, 4);
                if (file.gcount() == 4) info.subtype = std::string(compression, 4);
            } else {
                info.subtype = "PCM_S" + std::to_string(bits_per_sample);
            }
            
        } else if (std::strncmp(chunk_id, "SSND", 4) == 0) {
            info.data_offset = static_cast<int64_t>(chunk_pos);
            file.seekg(8, std::ios::cur);
            info.data_size = static_cast<int64_t>(chunk_size) - 8;
            
            if (info.frames == 0 && info.bytes_per_frame() > 0) 
                info.frames = info.data_size / info.bytes_per_frame();
            
            break;
        }
        
        file.seekg(chunk_pos + static_cast<std::streamoff>(chunk_size));
        if (chunk_size % 2 != 0) file.seekg(1, std::ios::cur);
    }
    
    return {info, std::nullopt};
}

Result<AudioInfo> validate_with_sndfile(const std::filesystem::path& path, AudioInfo&& info) {
    SF_INFO sf_info{};
    sf_info.channels = info.channels;
    sf_info.samplerate = info.sample_rate;
    sf_info.frames = info.frames;
    
    SNDFILE* sf = sf_open(path.string().c_str(), SFM_READ, &sf_info);
    if (!sf) return {info, std::nullopt};
    
    info.sample_rate = sf_info.samplerate;
    info.channels = sf_info.channels;
    info.frames = sf_info.frames;
    
    sf_close(sf);
    return {info, std::nullopt};
}

Result<AudioFile> AudioFile::open(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {std::nullopt, AudioError::FileNotFound};
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {std::nullopt, AudioError::FileNotFound};
    
    char magic[4];
    file.read(magic, 4);
    if (file.gcount() != 4) return {std::nullopt, AudioError::InvalidFormat};
    
    file.seekg(0, std::ios::beg);
    
    Result<AudioInfo> parse_result;
    if (std::strncmp(magic, "RIFF", 4) == 0 || std::strncmp(magic, "RF64", 4) == 0) {
        parse_result = parse_wav_header(file);
    } else if (std::strncmp(magic, "FORM", 4) == 0) {
        parse_result = parse_aiff_header(file);
    } else {
        return {std::nullopt, AudioError::UnsupportedFormat};
    }
    
    if (!parse_result.ok()) return {std::nullopt, *parse_result.error};
    
    AudioInfo info = *parse_result.value;
    auto validated = validate_with_sndfile(path, std::move(info));
    if (!validated.ok()) return {std::nullopt, *validated.error};
    
    AudioFile af;
    af.path_ = path;
    af.info_ = *validated.value;
    
    return {af, std::nullopt};
}

Result<std::vector<std::byte>> AudioFile::read_raw_samples(int64_t start_sample, int64_t end_sample) const {
    if (start_sample < 0 || end_sample < start_sample) 
        return {std::nullopt, AudioError::InvalidRange};
    if (end_sample >= info_.frames) 
        return {std::nullopt, AudioError::InvalidRange};
    
    int64_t bytes_per_frame = info_.bytes_per_frame();
    if (bytes_per_frame == 0) return {std::nullopt, AudioError::InvalidFormat};
    
    auto start_byte = start_sample * bytes_per_frame;
    auto end_byte = (end_sample + 1) * bytes_per_frame;
    auto num_bytes = end_byte - start_byte;
    
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) return {std::nullopt, AudioError::ReadError};
    
    file.seekg(info_.data_offset + start_byte);
    if (!file.good()) return {std::nullopt, AudioError::ReadError};
    
    std::vector<std::byte> buffer(static_cast<size_t>(num_bytes));
    file.read(reinterpret_cast<char*>(buffer.data()), num_bytes);
    
    if (file.gcount() != num_bytes) return {std::nullopt, AudioError::ReadError};
    
    return {buffer, std::nullopt};
}

} // namespace mwaac