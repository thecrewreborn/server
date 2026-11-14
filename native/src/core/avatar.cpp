#include "core/avatar.h"

#include <cstring>

#include "mbedtls/md.h"
#include "zlib.h"

namespace tcr {
namespace avatar {

namespace {

#include "core/avatar_logo.inc"

const char kSignature[] = "\x89PNG\r\n\x1a\n";

uint32_t be32(const std::string& d, size_t at) {
  return static_cast<uint32_t>(static_cast<uint8_t>(d[at])) << 24 |
         static_cast<uint32_t>(static_cast<uint8_t>(d[at + 1])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(d[at + 2])) << 8 | static_cast<uint8_t>(d[at + 3]);
}

void put32(std::string& out, uint32_t v) {
  out += static_cast<char>(v >> 24);
  out += static_cast<char>(v >> 16);
  out += static_cast<char>(v >> 8);
  out += static_cast<char>(v);
}

bool has_signature(const std::string& data) { return data.compare(0, 8, kSignature, 8) == 0 && data.size() >= 8; }

std::string slice(const std::string& d, uint64_t a, uint64_t b) {
  if (a >= d.size() || b <= a) return std::string();
  return d.substr(static_cast<size_t>(a), static_cast<size_t>(std::min<uint64_t>(b, d.size()) - a));
}

std::string chunk(const char* kind, const std::string& data) {
  std::string out;
  put32(out, static_cast<uint32_t>(data.size()));
  std::string body = std::string(kind, 4) + data;
  out += body;
  uLong crc = crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()));
  put32(out, static_cast<uint32_t>(crc));
  return out;
}

std::string zlib_error_text(int err, const char* msg, const char* what) {
  std::string text = "Error " + std::to_string(err) + " while " + what;
  if (msg) {
    text += std::string(": ") + std::string(msg).substr(0, 200);
  } else if (err == Z_BUF_ERROR) {
    text += ": incomplete or truncated stream";
  } else if (err == Z_STREAM_ERROR) {
    text += ": inconsistent stream state";
  } else if (err == Z_DATA_ERROR) {
    text += ": invalid input data";
  }
  return text;
}

bool compress9(const std::string& data, std::string* out, Raised* raised) {
  z_stream s;
  std::memset(&s, 0, sizeof(s));
  int err = deflateInit2(&s, 9, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  if (err != Z_OK) {
    if (err == Z_MEM_ERROR) {
      *raised = {"MemoryError", "Out of memory while compressing data"};
    } else if (err == Z_STREAM_ERROR) {
      *raised = {"zlib.error", "Bad compression level"};
    } else {
      *raised = {"zlib.error", zlib_error_text(err, s.msg, "compressing data")};
    }
    deflateEnd(&s);
    return false;
  }
  out->assign(deflateBound(&s, static_cast<uLong>(data.size())) + 16, '\0');
  s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  s.avail_in = static_cast<uInt>(data.size());
  s.next_out = reinterpret_cast<Bytef*>(&(*out)[0]);
  s.avail_out = static_cast<uInt>(out->size());
  err = deflate(&s, Z_FINISH);
  if (err != Z_STREAM_END) {
    *raised = {"zlib.error", zlib_error_text(err, s.msg, "compressing data")};
    deflateEnd(&s);
    return false;
  }
  out->resize(s.total_out);
  deflateEnd(&s);
  return true;
}

bool decompress(const std::string& data, size_t max_length, std::string* out, Raised* raised) {
  z_stream s;
  std::memset(&s, 0, sizeof(s));
  if (inflateInit2(&s, MAX_WBITS) != Z_OK) {
    *raised = {"MemoryError", "Can't allocate memory for decompression object"};
    return false;
  }
  s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  s.avail_in = static_cast<uInt>(data.size());
  out->clear();
  int err = Z_OK;
  size_t step = 16384;
  while (true) {
    size_t room = max_length ? max_length - out->size() : step;
    if (max_length && room == 0) break;
    if (!max_length && step < (1u << 26)) step *= 2;
    size_t have = out->size();
    out->resize(have + room);
    s.next_out = reinterpret_cast<Bytef*>(&(*out)[have]);
    s.avail_out = static_cast<uInt>(room);
    err = inflate(&s, Z_SYNC_FLUSH);
    out->resize(have + (room - s.avail_out));
    if (err != Z_OK && err != Z_BUF_ERROR && err != Z_STREAM_END) break;
    if (s.avail_out != 0 || err == Z_STREAM_END) break;
  }
  bool ok = err == Z_OK || err == Z_BUF_ERROR || err == Z_STREAM_END;
  if (!ok) {
    std::string msg = s.msg ? s.msg : "";
    if ((msg == "invalid distance code" || msg == "invalid literal/length code") && s.avail_in >= 24 &&
        s.avail_out >= 323) {
      msg = "invalid literal/length/distance code";
    }
    *raised = {"zlib.error", zlib_error_text(err, msg.empty() ? nullptr : msg.c_str(), "decompressing data")};
  }
  inflateEnd(&s);
  return ok;
}

int paeth(int a, int b, int c) {
  int p = a + b - c;
  int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

std::string sha256(const std::string& data) {
  uint8_t out[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t*>(data.data()),
             data.size(), out);
  return std::string(reinterpret_cast<const char*>(out), sizeof(out));
}

}

bool match_name(const std::string& path, const char* prefix, const char* suffix, std::string* name) {
  size_t a = std::char_traits<char>::length(prefix), b = std::char_traits<char>::length(suffix);
  if (path.size() < a + b + 3 || path.size() > a + b + 16) return false;
  if (path.compare(0, a, prefix) != 0 || path.compare(path.size() - b, b, suffix) != 0) return false;
  std::string n = path.substr(a, path.size() - a - b);
  for (char c : n) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
      return false;
    }
  }
  *name = n;
  return true;
}

const std::string& logo() {
  static const std::string kData(reinterpret_cast<const char*>(kLogo), sizeof(kLogo));
  return kData;
}

bool encode(uint32_t width, uint32_t height, const std::string& rgba, std::string* png, Raised* raised) {
  uint64_t stride = 4ull * width;
  std::string rows;
  for (uint64_t y = 0; y < height; y++) {
    rows += '\0';
    rows += slice(rgba, y * stride, (y + 1) * stride);
  }
  std::string head;
  put32(head, width);
  put32(head, height);
  head += std::string("\x08\x06\x00\x00\x00", 5);
  std::string idat;
  if (!compress9(rows, &idat, raised)) return false;
  *png = std::string(kSignature, 8) + chunk("IHDR", head) + chunk("IDAT", idat) + chunk("IEND", "");
  return true;
}

bool decode(const std::string& data, uint32_t* width_out, uint32_t* height_out, std::string* rgba_out,
            Raised* raised) {
  if (!has_signature(data)) {
    *raised = {"ValueError", "not a PNG"};
    return false;
  }
  uint64_t at = 8;
  std::string idat;
  bool have_head = false;
  uint32_t width = 0, height = 0;
  int depth = 0, color = 0, interlace = 0;
  while (at < data.size()) {
    if (data.size() - at < 8) {
      *raised = {"struct.error", "unpack requires a buffer of 8 bytes"};
      return false;
    }
    uint32_t n = be32(data, static_cast<size_t>(at));
    std::string kind = data.substr(static_cast<size_t>(at) + 4, 4);
    if (kind == "IHDR") {
      if (at + 8 > data.size() || data.size() - (at + 8) < 13) {
        *raised = {"struct.error", "unpack requires a buffer of 13 bytes"};
        return false;
      }
      size_t h = static_cast<size_t>(at) + 8;
      width = be32(data, h);
      height = be32(data, h + 4);
      depth = static_cast<uint8_t>(data[h + 8]);
      color = static_cast<uint8_t>(data[h + 9]);
      interlace = static_cast<uint8_t>(data[h + 12]);
      have_head = true;
    } else if (kind == "IDAT") {
      idat += slice(data, at + 8, at + 8 + n);
    }
    at += 12 + static_cast<uint64_t>(n);
  }
  if (!have_head) {
    *raised = {"TypeError", "cannot unpack non-iterable NoneType object"};
    return false;
  }
  int channels = color == 0 ? 1 : color == 2 ? 3 : color == 4 ? 2 : color == 6 ? 4 : 0;
  if (depth != 8 || interlace || !channels) {
    *raised = {"ValueError", "unsupported PNG (depth " + std::to_string(depth) + ", color " + std::to_string(color) +
                                 ", interlace " + std::to_string(interlace) + ")"};
    return false;
  }
  if (width > kMaxSide || height > kMaxSide) {
    *raised = {"ValueError",
               "picture too large (" + std::to_string(width) + "x" + std::to_string(height) + ")"};
    return false;
  }
  if (!width || !height) {
    *raised = {"ValueError",
               "picture without pixels (" + std::to_string(width) + "x" + std::to_string(height) + ")"};
    return false;
  }
  size_t stride = static_cast<size_t>(channels) * width;
  std::string raw;
  if (!decompress(idat, static_cast<size_t>(height) * (stride + 1), &raw, raised)) return false;
  std::string out;
  out.reserve(static_cast<size_t>(height) * stride);
  std::string prev(stride, '\0');
  for (size_t y = 0; y < height; y++) {
    size_t start = y * (stride + 1);
    if (start >= raw.size()) {
      *raised = {"IndexError", "index out of range"};
      return false;
    }
    int kind = static_cast<uint8_t>(raw[start]);
    std::string line = slice(raw, start + 1, (y + 1) * (stride + 1));
    for (size_t i = 0; i < stride; i++) {
      if (i >= line.size()) {
        *raised = {"IndexError", "bytearray index out of range"};
        return false;
      }
      if (kind > 4) {
        *raised = {"IndexError", "tuple index out of range"};
        return false;
      }
      int a = i >= static_cast<size_t>(channels) ? static_cast<uint8_t>(line[i - channels]) : 0;
      int c = i >= static_cast<size_t>(channels) ? static_cast<uint8_t>(prev[i - channels]) : 0;
      int b = static_cast<uint8_t>(prev[i]);
      int add = kind == 0 ? 0 : kind == 1 ? a : kind == 2 ? b : kind == 3 ? (a + b) / 2 : paeth(a, b, c);
      line[i] = static_cast<char>((static_cast<uint8_t>(line[i]) + add) & 0xFF);
    }
    out += line;
    prev = line;
  }
  *width_out = width;
  *height_out = height;
  if (channels == 4) {
    *rgba_out = std::move(out);
    return true;
  }
  std::string rgba;
  rgba.reserve(out.size() / channels * 4);
  for (size_t i = 0; i + channels <= out.size(); i += channels) {
    if (channels == 1) {
      rgba += std::string(3, out[i]);
      rgba += '\xff';
    } else if (channels == 2) {
      rgba += std::string(3, out[i]);
      rgba += out[i + 1];
    } else {
      rgba += out.substr(i, 3);
      rgba += '\xff';
    }
  }
  *rgba_out = std::move(rgba);
  return true;
}

std::string box(uint32_t width, uint32_t height, const std::string& rgba, uint32_t size) {
  std::string out;
  out.reserve(4ull * size * size);
  for (uint64_t ty = 0; ty < size; ty++) {
    uint64_t y0 = ty * height / size, y1 = std::max(ty * height / size + 1, (ty + 1) * height / size);
    for (uint64_t tx = 0; tx < size; tx++) {
      uint64_t x0 = tx * width / size, x1 = std::max(tx * width / size + 1, (tx + 1) * width / size);
      uint64_t sums[4] = {0, 0, 0, 0};
      uint64_t n = (y1 - y0) * (x1 - x0);
      for (uint64_t y = y0; y < y1; y++) {
        uint64_t a = 4 * (y * width + x0), b = std::min<uint64_t>(4 * (y * width + x1), rgba.size());
        for (uint64_t k = a; k < b; k++) sums[(k - a) % 4] += static_cast<uint8_t>(rgba[static_cast<size_t>(k)]);
      }
      for (int ch = 0; ch < 4; ch++) out += static_cast<char>(sums[ch] / n);
    }
  }
  return out;
}

bool fit(const std::string& data, std::string* out, Raised* raised) {
  if (data.size() < 29) {
    *raised = {"struct.error", "unpack requires a buffer of 13 bytes"};
    return false;
  }
  uint32_t width = be32(data, 16), height = be32(data, 20);
  int depth = static_cast<uint8_t>(data[24]), color = static_cast<uint8_t>(data[25]);
  int interlace = static_cast<uint8_t>(data[28]);
  if (width > kMaxSide || height > kMaxSide) {
    *raised = {"ValueError", "picture too large (" + std::to_string(width) + "x" + std::to_string(height) + ")"};
    return false;
  }
  bool square = width == height && (width & (width - 1)) == 0 && width <= static_cast<uint32_t>(kFit);
  if (has_signature(data) && square && depth == 8 && color == 6 && interlace == 0) {
    *out = data;
    return true;
  }
  uint32_t w = 0, h = 0;
  std::string rgba;
  if (!decode(data, &w, &h, &rgba, raised)) return false;
  return square ? encode(w, h, rgba, out, raised) : encode(kFit, kFit, box(w, h, rgba, kFit), out, raised);
}

std::optional<std::string> check_console_picture(const std::string& data, bool decode_it) {
  if (data.size() > kMaxUpload) return "picture larger than " + std::to_string(kMaxUpload) + " bytes";
  if (!has_signature(data) || slice(data, 12, 16) != "IHDR" || data.size() < 33) return std::string("not a PNG");
  uint32_t width = be32(data, 16), height = be32(data, 20);
  if (width > kMaxSide || height > kMaxSide) {
    return "picture larger than " + std::to_string(kMaxSide) + "x" + std::to_string(kMaxSide);
  }
  if (!width || !height) return std::string("picture without pixels");
  if (!decode_it) return std::nullopt;
  uint32_t w = 0, h = 0;
  std::string rgba;
  Raised raised;
  if (!decode(data, &w, &h, &rgba, &raised)) return std::string("unsupported PNG");
  return std::nullopt;
}

bool Avatars::valid_name(const std::optional<std::string>& name) const {
  return name && !name->empty() && paths_.basename(*name) == *name;
}

std::string Avatars::console_path(const std::string& name) const {
  return paths_.join(paths_.join(folder_, kConsoleFolder), name + ".png");
}

bool Avatars::fitted(const std::string& path, double mtime, std::string* out, Raised* raised) {
  for (auto it = fitted_.begin(); it != fitted_.end(); ++it) {
    if (it->first.first == path && it->first.second == mtime) {
      fitted_.splice(fitted_.end(), fitted_, it);
      *out = fitted_.back().second;
      return true;
    }
  }
  std::string data;
  OsError err;
  if (!files_.read(path, &data, &err)) return false;
  Raised why;
  if (!fit(data, out, &why)) {
    if (why.type == "MemoryError") *raised = why;
    return false;
  }
  fitted_.emplace_back(std::make_pair(path, mtime), *out);
  if (fitted_.size() > 16) fitted_.pop_front();
  return true;
}

std::string Avatars::for_request(const std::optional<std::string>& name_in, const std::string& path,
                                 Raised* raised) {
  bool own_profile = path.size() == 23 && path.compare(0, 11, "/avatars/0x") == 0 &&
                     path.compare(19, 4, ".png") == 0;
  if (own_profile) {
    for (size_t i = 11; i < 19; i++) {
      char c = path[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) own_profile = false;
    }
  }
  std::optional<std::string> name = name_in;
  if (path.compare(0, 14, "/avatars/name/") == 0) {
    std::string n;
    name.reset();
    if (match_name(path, "/avatars/name/", ".png", &n)) name = n;
  }
  if (valid_name(name)) {
    std::string pc = paths_.join(folder_, *name + ".png"), console = console_path(*name);
    const std::string* order[2] = {own_profile ? &console : &pc, own_profile ? &pc : &console};
    for (const std::string* file : order) {
      FileStat st;
      OsError err;
      if (!files_.stat(*file, &st, &err)) continue;
      int64_t sec = st.mtime_ns / 1000000000, nsec = st.mtime_ns % 1000000000;
      if (nsec < 0) {
        nsec += 1000000000;
        sec -= 1;
      }
      double mtime = static_cast<double>(sec) + static_cast<double>(nsec) * 1e-9;
      std::string out;
      if (fitted(*file, mtime, &out, raised)) return out;
      if (!raised->type.empty()) return std::string();
    }
  }
  return logo();
}

std::optional<std::string> Avatars::rgba(const std::string& png, uint32_t size, const std::string& client_ip,
                                         Raised* raised) {
  std::string key = sha256(png) + std::to_string(size);
  auto found = rgba_index_.find(key);
  if (found != rgba_index_.end()) {
    rgba_entries_.splice(rgba_entries_.end(), rgba_entries_, found->second);
    return rgba_entries_.back().second;
  }
  if (!decodes_.allow(client_ip, env_.monotonic())) return std::nullopt;
  uint32_t width = 0, height = 0;
  std::string pixels;
  if (!decode(png, &width, &height, &pixels, raised)) return std::nullopt;
  if (!(width == height && height == size)) pixels = box(width, height, pixels, size);
  rgba_entries_.emplace_back(key, pixels);
  rgba_index_[key] = std::prev(rgba_entries_.end());
  rgba_size_ += pixels.size();
  while (rgba_size_ > kRgbaCacheBytes && rgba_entries_.size() > 1) {
    rgba_size_ -= rgba_entries_.front().second.size();
    rgba_index_.erase(rgba_entries_.front().first);
    rgba_entries_.pop_front();
  }
  return pixels;
}

std::optional<std::string> Avatars::rgba_for(const std::optional<std::string>& name, uint32_t size,
                                             const std::string& client_ip, Raised* raised) {
  std::string path = name && !name->empty() ? "/avatars/name/" + *name + ".png" : std::string();
  std::string png = for_request(name, path, raised);
  if (!raised->type.empty()) return std::nullopt;
  return rgba(png, size, client_ip, raised);
}

std::optional<bool> Avatars::save_upload(const std::string& name, const std::string& rgba, Raised* raised) {
  if (rgba.size() != static_cast<size_t>(kFit) * kFit * 4 || paths_.basename(name) != name) return false;
  OsError err;
  if (!files_.makedirs(folder_, &err)) {
    *raised = {err.type(), err.text()};
    return std::nullopt;
  }
  std::string png;
  if (!encode(kFit, kFit, rgba, &png, raised)) return std::nullopt;
  FileStat st;
  if (!files_.write_atomic(paths_.join(folder_, name + ".png"), png, false, -1, -1, &st, &err)) {
    *raised = {err.type(), err.text()};
    return std::nullopt;
  }
  return true;
}

std::optional<std::string> Avatars::save_console_upload(const std::string& name, const std::string& data,
                                                        Raised* raised) {
  if (!valid_name(name)) return std::string("bad name");
  std::optional<std::string> why = check_console_picture(data);
  if (why) return why;
  OsError err;
  if (!files_.makedirs(paths_.join(folder_, kConsoleFolder), &err)) {
    *raised = {err.type(), err.text()};
    return std::nullopt;
  }
  FileStat st;
  if (!files_.write_atomic(console_path(name), data, false, -1, -1, &st, &err)) {
    *raised = {err.type(), err.text()};
    return std::nullopt;
  }
  return std::string();
}

}
}
