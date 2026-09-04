#include "animgraph/asset/codec.hpp"

#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace animgraph {
namespace {

constexpr std::array<char, 8> skeleton_magic{'A', 'G', 'S', 'K', 'E', 'L', '1', '\0'};
constexpr std::array<char, 8> clip_magic{'A', 'G', 'C', 'L', 'I', 'P', '1', '\0'};
constexpr std::array<char, 8> graph_magic{'A', 'G', 'G', 'R', 'A', 'P', 'H', '1'};
constexpr std::uint16_t little_endian_marker = 0xFEFF;
constexpr std::uint32_t skeleton_header_size = 40;
constexpr std::uint32_t skeleton_record_size = 104;
constexpr std::uint32_t clip_header_size = 80;
constexpr std::uint32_t track_record_size = 28;
constexpr std::uint32_t event_record_size = 20;
constexpr std::uint32_t marker_record_size = 16;
constexpr std::uint32_t vec3_key_size = 20;
constexpr std::uint32_t quat_key_size = 24;
constexpr std::size_t skeleton_crc_offset = 36;
constexpr std::size_t clip_crc_offset = 76;
constexpr std::uint32_t graph_header_size = 36;
constexpr std::size_t graph_crc_offset = 32;

class Writer {
 public:
  void u16(std::uint16_t value) {
    data_.push_back(static_cast<std::byte>(value & 0xFFU));
    data_.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  }
  void u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      data_.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
  }
  void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void i64(std::int64_t value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned shift = 0; shift < 64; shift += 8) {
      data_.push_back(static_cast<std::byte>((bits >> shift) & 0xFFU));
    }
  }
  void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
  void magic(const std::array<char, 8>& value) {
    for (char character : value) data_.push_back(static_cast<std::byte>(character));
  }
  void bytes(std::span<const std::byte> value) { data_.insert(data_.end(), value.begin(), value.end()); }
  void string(std::string_view value) {
    for (char character : value) data_.push_back(static_cast<std::byte>(character));
  }
  void zeros(std::size_t count) { data_.insert(data_.end(), count, std::byte{0}); }
  void patch_u32(std::size_t offset, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
      data_[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
    }
  }
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
  [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return data_; }
  [[nodiscard]] std::vector<std::byte> take() && { return std::move(data_); }

 private:
  std::vector<std::byte> data_;
};

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes, std::size_t offset = 0)
      : bytes_(bytes), cursor_(offset) {}

  std::uint16_t u16() {
    if (!require(2)) return 0;
    const auto value = static_cast<std::uint16_t>(
        at(cursor_) | (at(cursor_ + 1) << 8U));
    cursor_ += 2;
    return value;
  }
  std::uint32_t u32() {
    if (!require(4)) return 0;
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) value |= at(cursor_ + byte) << (byte * 8U);
    cursor_ += 4;
    return value;
  }
  std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
  std::int64_t i64() {
    if (!require(8)) return 0;
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
      value |= static_cast<std::uint64_t>(at(cursor_ + byte)) << (byte * 8U);
    }
    cursor_ += 8;
    return std::bit_cast<std::int64_t>(value);
  }
  float f32() { return std::bit_cast<float>(u32()); }
  [[nodiscard]] bool ok() const noexcept { return ok_; }

 private:
  [[nodiscard]] std::uint32_t at(std::size_t offset) const {
    return std::to_integer<std::uint8_t>(bytes_[offset]);
  }
  bool require(std::size_t count) {
    if (!ok_ || cursor_ > bytes_.size() || count > bytes_.size() - cursor_) {
      ok_ = false;
      return false;
    }
    return true;
  }
  std::span<const std::byte> bytes_;
  std::size_t cursor_{};
  bool ok_{true};
};

Expected<std::uint32_t, Error> narrow_u32(std::size_t value) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    return make_unexpected(Error{ErrorCode::bounds, "asset offset exceeds 32 bits"});
  }
  return static_cast<std::uint32_t>(value);
}

bool region(std::size_t offset, std::size_t count, std::size_t stride, std::size_t limit) {
  return offset <= limit && (stride == 0 || count <= (limit - offset) / stride);
}

std::uint32_t crc32(std::span<const std::byte> bytes, std::size_t zero_offset) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const std::uint8_t input = index >= zero_offset && index < zero_offset + 4
                                   ? 0
                                   : std::to_integer<std::uint8_t>(bytes[index]);
    crc ^= input;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void write_transform(Writer& writer, const Transform& transform) {
  writer.f32(transform.translation.x); writer.f32(transform.translation.y); writer.f32(transform.translation.z);
  writer.f32(transform.rotation.x); writer.f32(transform.rotation.y);
  writer.f32(transform.rotation.z); writer.f32(transform.rotation.w);
  writer.f32(transform.scale.x); writer.f32(transform.scale.y); writer.f32(transform.scale.z);
}

Transform read_transform(Reader& reader) {
  Transform result;
  result.translation = {reader.f32(), reader.f32(), reader.f32()};
  result.rotation = {reader.f32(), reader.f32(), reader.f32(), reader.f32()};
  result.scale = {reader.f32(), reader.f32(), reader.f32()};
  return result;
}

struct StringRef { std::uint32_t offset{}; std::uint32_t length{}; };

Expected<StringRef, Error> append_string(Writer& strings, std::string_view value) {
  const auto offset = narrow_u32(strings.size());
  const auto length = narrow_u32(value.size());
  if (!offset || !length) return make_unexpected(Error{ErrorCode::bounds, "string table overflow"});
  strings.string(value);
  return StringRef{*offset, *length};
}

Expected<std::string, Error> read_string(std::span<const std::byte> bytes,
                                         std::size_t strings_offset,
                                         std::size_t strings_size,
                                         std::uint32_t relative,
                                         std::uint32_t length_value) {
  if (!region(relative, length_value, 1, strings_size) ||
      !region(strings_offset + relative, length_value, 1, bytes.size())) {
    return make_unexpected(Error{ErrorCode::bounds, "string reference is out of bounds"});
  }
  std::string result;
  result.reserve(length_value);
  for (std::size_t index = 0; index < length_value; ++index) {
    result.push_back(static_cast<char>(std::to_integer<std::uint8_t>(bytes[strings_offset + relative + index])));
  }
  return result;
}

bool magic_equals(std::span<const std::byte> bytes, const std::array<char, 8>& magic) {
  if (bytes.size() < magic.size()) return false;
  for (std::size_t index = 0; index < magic.size(); ++index) {
    if (std::to_integer<std::uint8_t>(bytes[index]) != static_cast<std::uint8_t>(magic[index])) return false;
  }
  return true;
}

class JsonSyntaxValidator {
 public:
  explicit JsonSyntaxValidator(std::string_view text) : text_(text) {}
  bool valid() {
    skip_space();
    if (!value()) return false;
    skip_space();
    return cursor_ == text_.size();
  }

 private:
  void skip_space() {
    while (cursor_ < text_.size() && (text_[cursor_] == ' ' || text_[cursor_] == '\n' ||
           text_[cursor_] == '\r' || text_[cursor_] == '\t')) ++cursor_;
  }
  bool consume(char value) {
    skip_space();
    if (cursor_ >= text_.size() || text_[cursor_] != value) return false;
    ++cursor_;
    return true;
  }
  bool literal(std::string_view value) {
    if (text_.substr(cursor_, value.size()) != value) return false;
    cursor_ += value.size();
    return true;
  }
  bool string() {
    if (!consume('"')) return false;
    while (cursor_ < text_.size()) {
      const char character = text_[cursor_++];
      if (character == '"') return true;
      if (static_cast<unsigned char>(character) < 0x20U) return false;
      if (character == '\\') {
        if (cursor_ >= text_.size()) return false;
        const char escape = text_[cursor_++];
        if (escape == 'u') {
          for (int digit = 0; digit < 4; ++digit) {
            if (cursor_ >= text_.size() || !std::isxdigit(
                    static_cast<unsigned char>(text_[cursor_++]))) return false;
          }
        } else if (std::string_view{"\"\\/bfnrt"}.find(escape) == std::string_view::npos) {
          return false;
        }
      }
    }
    return false;
  }
  bool number() {
    const std::size_t start = cursor_;
    if (cursor_ < text_.size() && text_[cursor_] == '-') ++cursor_;
    if (cursor_ >= text_.size()) return false;
    if (text_[cursor_] == '0') ++cursor_;
    else {
      if (text_[cursor_] < '1' || text_[cursor_] > '9') return false;
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') ++cursor_;
    }
    if (cursor_ < text_.size() && text_[cursor_] == '.') {
      ++cursor_;
      const auto fraction = cursor_;
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') ++cursor_;
      if (fraction == cursor_) return false;
    }
    if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E')) {
      ++cursor_;
      if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-')) ++cursor_;
      const auto exponent = cursor_;
      while (cursor_ < text_.size() && text_[cursor_] >= '0' && text_[cursor_] <= '9') ++cursor_;
      if (exponent == cursor_) return false;
    }
    return cursor_ > start;
  }
  bool array() {
    if (!consume('[')) return false;
    skip_space();
    if (consume(']')) return true;
    do {
      if (!value()) return false;
      skip_space();
      if (consume(']')) return true;
    } while (consume(','));
    return false;
  }
  bool object() {
    if (!consume('{')) return false;
    skip_space();
    if (consume('}')) return true;
    do {
      if (!string() || !consume(':') || !value()) return false;
      skip_space();
      if (consume('}')) return true;
    } while (consume(','));
    return false;
  }
  bool value() {
    skip_space();
    if (cursor_ >= text_.size()) return false;
    switch (text_[cursor_]) {
      case '{': return object();
      case '[': return array();
      case '"': return string();
      case 't': return literal("true");
      case 'f': return literal("false");
      case 'n': return literal("null");
      default: return number();
    }
  }
  std::string_view text_;
  std::size_t cursor_{};
};

bool valid_graph_plan_json(std::string_view plan, std::uint32_t node_count) {
  if (!JsonSyntaxValidator{plan}.valid() || !plan.starts_with("{\"version\":") ||
      plan.find("\"instructions\":[") == std::string_view::npos) return false;
  std::size_t count = 0;
  std::size_t cursor = 0;
  while ((cursor = plan.find("{\"id\":", cursor)) != std::string_view::npos) {
    ++count;
    cursor += 6;
  }
  return count == node_count;
}

Expected<void, Error> basic_size(std::span<const std::byte> bytes, std::size_t header) {
  if (bytes.size() < header || bytes.size() > max_asset_bytes) {
    return make_unexpected(Error{ErrorCode::bounds, "asset size is outside limits"});
  }
  return {};
}

}  // namespace

Expected<std::vector<std::byte>, Error> encode_skeleton(const CompiledSkeleton& skeleton) {
  if (skeleton.joints.empty() || skeleton.joints.size() > max_joints ||
      skeleton.original_to_compiled.size() != skeleton.joints.size() ||
      skeleton.compiled_to_original.size() != skeleton.joints.size()) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "compiled skeleton layout is invalid"});
  }
  std::vector<bool> originals(skeleton.joints.size(), false);
  for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
    const auto& joint = skeleton.joints[index];
    if (joint.id.value != index || joint.name.empty() || !finite(joint.reference_local) ||
        !finite(joint.inverse_bind) || (joint.parent && joint.parent->value >= index) ||
        joint.original_index >= skeleton.joints.size() || originals[joint.original_index]) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "compiled joint is not canonical"});
    }
    originals[joint.original_index] = true;
  }

  Writer strings;
  std::vector<StringRef> names;
  std::vector<StringRef> semantics;
  for (const auto& joint : skeleton.joints) {
    const auto name = append_string(strings, joint.name);
    const auto semantic = append_string(strings, joint.semantic.value_or(""));
    if (!name || !semantic) return make_unexpected(Error{ErrorCode::bounds, "string table overflow"});
    names.push_back(*name);
    semantics.push_back(*semantic);
  }

  Writer writer;
  writer.magic(skeleton_magic);
  writer.u16(skeleton_asset_version); writer.u16(little_endian_marker);
  writer.u32(skeleton_header_size); writer.u32(0); writer.u32(skeleton_header_size);
  writer.u32(static_cast<std::uint32_t>(skeleton.joints.size())); writer.u32(0); writer.u32(0); writer.u32(0);
  for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
    const auto& joint = skeleton.joints[index];
    writer.i32(joint.parent ? static_cast<std::int32_t>(joint.parent->value) : -1);
    writer.u32(joint.original_index);
    writer.u32(names[index].offset); writer.u32(names[index].length);
    writer.u32(semantics[index].offset); writer.u32(semantics[index].length);
    write_transform(writer, joint.reference_local);
    write_transform(writer, joint.inverse_bind);
  }
  const auto strings_offset = narrow_u32(writer.size());
  if (!strings_offset) return make_unexpected(strings_offset.error());
  writer.bytes(strings.data());
  const auto file_size = narrow_u32(writer.size());
  const auto string_size = narrow_u32(strings.size());
  if (!file_size || !string_size || writer.size() > max_asset_bytes)
    return make_unexpected(Error{ErrorCode::bounds, "skeleton asset exceeds limit"});
  writer.patch_u32(16, *file_size); writer.patch_u32(28, *strings_offset);
  writer.patch_u32(32, *string_size); writer.patch_u32(36, 0);
  writer.patch_u32(36, crc32(writer.data(), skeleton_crc_offset));
  return std::move(writer).take();
}

Expected<CompiledSkeleton, Error> decode_skeleton(std::span<const std::byte> bytes) {
  const auto sized = basic_size(bytes, skeleton_header_size);
  if (!sized || !magic_equals(bytes, skeleton_magic))
    return make_unexpected(Error{ErrorCode::invalid_format, "invalid skeleton asset header"});
  Reader header(bytes, 8);
  const auto version = header.u16(); const auto endian = header.u16();
  const auto header_size = header.u32(); const auto file_size = header.u32();
  const auto records_offset = header.u32(); const auto count = header.u32();
  const auto strings_offset = header.u32(); const auto strings_size = header.u32();
  const auto stored_crc = header.u32();
  if (!header.ok() || version != skeleton_asset_version || endian != little_endian_marker ||
      header_size != skeleton_header_size || file_size != bytes.size() || count == 0 ||
      count > max_joints || records_offset != skeleton_header_size ||
      !region(records_offset, count, skeleton_record_size, strings_offset) ||
      strings_offset + strings_size != bytes.size() ||
      crc32(bytes, skeleton_crc_offset) != stored_crc) {
    return make_unexpected(Error{ErrorCode::invalid_format, "skeleton asset bounds or integrity failure"});
  }

  struct DecodedJoint { std::int32_t parent; std::uint32_t original; RawJoint raw; };
  std::vector<DecodedJoint> decoded;
  decoded.reserve(count);
  std::vector<bool> originals(count, false);
  for (std::size_t index = 0; index < count; ++index) {
    Reader record(bytes, records_offset + index * skeleton_record_size);
    const auto parent = record.i32(); const auto original = record.u32();
    const auto name_offset = record.u32(); const auto name_length = record.u32();
    const auto semantic_offset = record.u32(); const auto semantic_length = record.u32();
    const auto reference = read_transform(record); const auto inverse_bind = read_transform(record);
    const auto name = read_string(bytes, strings_offset, strings_size, name_offset, name_length);
    const auto semantic = read_string(bytes, strings_offset, strings_size, semantic_offset, semantic_length);
    if (!record.ok() || !name || !semantic || name->empty() || original >= count || originals[original] ||
        parent >= static_cast<std::int32_t>(index) || parent < -1 || !finite(reference) || !finite(inverse_bind)) {
      return make_unexpected(Error{ErrorCode::invalid_format, "skeleton joint record is invalid"});
    }
    originals[original] = true;
    decoded.push_back(DecodedJoint{parent, original,
                                  RawJoint{.name = *name, .parent = std::nullopt,
                                           .reference_local = reference, .inverse_bind = inverse_bind,
                                           .semantic = semantic->empty() ? std::nullopt
                                                                         : std::optional<std::string>{*semantic}}});
  }
  RawSkeleton raw;
  raw.joints.resize(count);
  for (std::size_t compiled_index = 0; compiled_index < count; ++compiled_index) {
    auto item = decoded[compiled_index];
    if (item.parent >= 0) item.raw.parent = decoded[static_cast<std::size_t>(item.parent)].original;
    raw.joints[item.original] = std::move(item.raw);
  }
  return compile_skeleton(raw);
}

Expected<std::vector<std::byte>, Error> encode_clip(const AnimationClip& clip) {
  const auto valid = validate_clip(clip);
  if (!valid) return make_unexpected(valid.error());
  if (clip.tracks.size() > max_joints) return make_unexpected(Error{ErrorCode::bounds, "too many clip tracks"});

  Writer strings;
  const auto clip_name = append_string(strings, clip.name);
  if (!clip_name) return make_unexpected(clip_name.error());
  std::vector<StringRef> event_names, marker_names;
  for (const auto& event : clip.events) {
    auto value = append_string(strings, event.name); if (!value) return make_unexpected(value.error());
    event_names.push_back(*value);
  }
  for (const auto& marker : clip.markers) {
    auto value = append_string(strings, marker.name); if (!value) return make_unexpected(value.error());
    marker_names.push_back(*value);
  }

  const std::uint32_t track_offset = clip_header_size;
  const std::uint32_t event_offset = track_offset + static_cast<std::uint32_t>(clip.tracks.size()) * track_record_size;
  const std::uint32_t marker_offset = event_offset + static_cast<std::uint32_t>(clip.events.size()) * event_record_size;
  Writer writer;
  writer.magic(clip_magic); writer.u16(clip_asset_version); writer.u16(little_endian_marker);
  writer.u32(clip_header_size); writer.u32(0);
  writer.u32(track_offset); writer.u32(static_cast<std::uint32_t>(clip.tracks.size()));
  writer.u32(event_offset); writer.u32(static_cast<std::uint32_t>(clip.events.size()));
  writer.u32(marker_offset); writer.u32(static_cast<std::uint32_t>(clip.markers.size()));
  writer.u32(0); writer.u32(0); writer.i64(clip.duration.ticks);
  writer.u32(static_cast<std::uint32_t>(clip.mode)); writer.u32(clip_name->offset);
  writer.u32(clip_name->length); writer.u32(0); writer.u32(0);
  writer.zeros(clip.tracks.size() * track_record_size);
  for (std::size_t index = 0; index < clip.events.size(); ++index) {
    writer.i64(clip.events[index].time.ticks); writer.u32(event_names[index].offset);
    writer.u32(event_names[index].length); writer.i32(clip.events[index].payload);
  }
  for (std::size_t index = 0; index < clip.markers.size(); ++index) {
    writer.i64(clip.markers[index].time.ticks); writer.u32(marker_names[index].offset);
    writer.u32(marker_names[index].length);
  }
  for (std::size_t index = 0; index < clip.tracks.size(); ++index) {
    const auto& track = clip.tracks[index];
    const std::size_t record = track_offset + index * track_record_size;
    writer.patch_u32(record, track.joint.value);
    auto offset = narrow_u32(writer.size()); if (!offset) return make_unexpected(offset.error());
    writer.patch_u32(record + 4, *offset); writer.patch_u32(record + 8, static_cast<std::uint32_t>(track.translations.size()));
    for (const auto& key : track.translations) {
      writer.i64(key.time.ticks); writer.f32(key.value.x); writer.f32(key.value.y); writer.f32(key.value.z);
    }
    offset = narrow_u32(writer.size()); if (!offset) return make_unexpected(offset.error());
    writer.patch_u32(record + 12, *offset); writer.patch_u32(record + 16, static_cast<std::uint32_t>(track.rotations.size()));
    for (const auto& key : track.rotations) {
      writer.i64(key.time.ticks); writer.f32(key.value.x); writer.f32(key.value.y);
      writer.f32(key.value.z); writer.f32(key.value.w);
    }
    offset = narrow_u32(writer.size()); if (!offset) return make_unexpected(offset.error());
    writer.patch_u32(record + 20, *offset); writer.patch_u32(record + 24, static_cast<std::uint32_t>(track.scales.size()));
    for (const auto& key : track.scales) {
      writer.i64(key.time.ticks); writer.f32(key.value.x); writer.f32(key.value.y); writer.f32(key.value.z);
    }
  }
  const auto strings_offset = narrow_u32(writer.size()); const auto strings_size = narrow_u32(strings.size());
  if (!strings_offset || !strings_size) return make_unexpected(Error{ErrorCode::bounds, "clip string table overflow"});
  writer.bytes(strings.data());
  const auto file_size = narrow_u32(writer.size());
  if (!file_size || writer.size() > max_asset_bytes) return make_unexpected(Error{ErrorCode::bounds, "clip asset exceeds limit"});
  writer.patch_u32(16, *file_size); writer.patch_u32(44, *strings_offset);
  writer.patch_u32(48, *strings_size); writer.patch_u32(76, 0);
  writer.patch_u32(76, crc32(writer.data(), clip_crc_offset));
  return std::move(writer).take();
}

Expected<AnimationClip, Error> decode_clip(std::span<const std::byte> bytes) {
  const auto sized = basic_size(bytes, clip_header_size);
  if (!sized || !magic_equals(bytes, clip_magic))
    return make_unexpected(Error{ErrorCode::invalid_format, "invalid clip asset header"});
  Reader header(bytes, 8);
  const auto version = header.u16(); const auto endian = header.u16();
  const auto header_size = header.u32(); const auto file_size = header.u32();
  const auto tracks_offset = header.u32(); const auto track_count = header.u32();
  const auto events_offset = header.u32(); const auto event_count = header.u32();
  const auto markers_offset = header.u32(); const auto marker_count = header.u32();
  const auto strings_offset = header.u32(); const auto strings_size = header.u32();
  const auto duration = header.i64(); const auto mode_value = header.u32();
  const auto name_offset = header.u32(); const auto name_length = header.u32();
  const auto reserved = header.u32();
  const auto stored_crc = header.u32();
  if (!header.ok() || version != clip_asset_version || endian != little_endian_marker ||
      header_size != clip_header_size || file_size != bytes.size() || track_count > max_joints ||
      event_count > max_events || marker_count > max_events || duration < 0 || mode_value > 2 || reserved != 0 ||
      tracks_offset != clip_header_size ||
      events_offset != tracks_offset + track_count * track_record_size ||
      markers_offset != events_offset + event_count * event_record_size ||
      !region(markers_offset, marker_count, marker_record_size, strings_offset) ||
      strings_offset + strings_size != bytes.size() || crc32(bytes, clip_crc_offset) != stored_crc) {
    return make_unexpected(Error{ErrorCode::invalid_format, "clip asset bounds or integrity failure"});
  }
  const auto clip_name = read_string(bytes, strings_offset, strings_size, name_offset, name_length);
  if (!clip_name) return make_unexpected(clip_name.error());
  AnimationClip clip;
  clip.name = *clip_name;
  clip.duration = AnimTime{duration};
  clip.mode = static_cast<ClipPlaybackMode>(mode_value);

  const std::size_t key_region = markers_offset + marker_count * marker_record_size;
  for (std::size_t index = 0; index < track_count; ++index) {
    Reader record(bytes, tracks_offset + index * track_record_size);
    JointTrack track; track.joint = JointId{record.u32()};
    const auto translation_offset = record.u32(); const auto translation_count = record.u32();
    const auto rotation_offset = record.u32(); const auto rotation_count = record.u32();
    const auto scale_offset = record.u32(); const auto scale_count = record.u32();
    if (!record.ok() || translation_count > max_track_keys || rotation_count > max_track_keys ||
        scale_count > max_track_keys || translation_offset < key_region || rotation_offset < key_region ||
        scale_offset < key_region || !region(translation_offset, translation_count, vec3_key_size, strings_offset) ||
        !region(rotation_offset, rotation_count, quat_key_size, strings_offset) ||
        !region(scale_offset, scale_count, vec3_key_size, strings_offset)) {
      return make_unexpected(Error{ErrorCode::bounds, "clip key region is invalid"});
    }
    Reader translations(bytes, translation_offset);
    for (std::size_t key = 0; key < translation_count; ++key) {
      track.translations.push_back({AnimTime{translations.i64()},
                                    Vec3{translations.f32(), translations.f32(), translations.f32()}});
    }
    Reader rotations(bytes, rotation_offset);
    for (std::size_t key = 0; key < rotation_count; ++key) {
      track.rotations.push_back({AnimTime{rotations.i64()},
                                 Quat{rotations.f32(), rotations.f32(), rotations.f32(), rotations.f32()}});
    }
    Reader scales(bytes, scale_offset);
    for (std::size_t key = 0; key < scale_count; ++key) {
      track.scales.push_back({AnimTime{scales.i64()},
                              Vec3{scales.f32(), scales.f32(), scales.f32()}});
    }
    if (!translations.ok() || !rotations.ok() || !scales.ok())
      return make_unexpected(Error{ErrorCode::bounds, "truncated clip key data"});
    clip.tracks.push_back(std::move(track));
  }
  for (std::size_t index = 0; index < event_count; ++index) {
    Reader event(bytes, events_offset + index * event_record_size);
    const auto time = event.i64(); const auto offset = event.u32(); const auto length = event.u32();
    const auto payload = event.i32(); const auto name = read_string(bytes, strings_offset, strings_size, offset, length);
    if (!event.ok() || !name) return make_unexpected(Error{ErrorCode::bounds, "invalid event record"});
    clip.events.push_back({AnimTime{time}, *name, payload});
  }
  for (std::size_t index = 0; index < marker_count; ++index) {
    Reader marker(bytes, markers_offset + index * marker_record_size);
    const auto time = marker.i64(); const auto offset = marker.u32(); const auto length = marker.u32();
    const auto name = read_string(bytes, strings_offset, strings_size, offset, length);
    if (!marker.ok() || !name) return make_unexpected(Error{ErrorCode::bounds, "invalid marker record"});
    clip.markers.push_back({AnimTime{time}, *name});
  }
  const auto valid = validate_clip(clip);
  if (!valid) return make_unexpected(valid.error());
  return clip;
}

Expected<std::vector<std::byte>, Error> encode_graph_plan(const CompiledGraph& graph) {
  const std::string plan = canonical_plan_json(graph);
  if (plan.empty() || plan.size() > max_asset_bytes - graph_header_size ||
      graph.instructions.empty() || graph.instructions.size() > max_graph_nodes) {
    return make_unexpected(Error{ErrorCode::bounds, "graph plan exceeds asset limits"});
  }
  Writer writer;
  writer.magic(graph_magic);
  writer.u16(graph_asset_version); writer.u16(little_endian_marker);
  writer.u32(graph_header_size); writer.u32(0); writer.u32(graph_header_size);
  writer.u32(static_cast<std::uint32_t>(plan.size()));
  writer.u32(static_cast<std::uint32_t>(graph.instructions.size())); writer.u32(0);
  writer.string(plan);
  const auto file_size = narrow_u32(writer.size());
  if (!file_size) return make_unexpected(file_size.error());
  writer.patch_u32(16, *file_size);
  writer.patch_u32(32, crc32(writer.data(), graph_crc_offset));
  return std::move(writer).take();
}

Expected<std::string, Error> decode_graph_plan(std::span<const std::byte> bytes) {
  const auto sized = basic_size(bytes, graph_header_size);
  if (!sized || !magic_equals(bytes, graph_magic))
    return make_unexpected(Error{ErrorCode::invalid_format, "invalid graph asset header"});
  Reader header(bytes, 8);
  const auto version = header.u16(); const auto endian = header.u16();
  const auto header_size = header.u32(); const auto file_size = header.u32();
  const auto payload_offset = header.u32(); const auto payload_size = header.u32();
  const auto node_count = header.u32(); const auto stored_crc = header.u32();
  if (!header.ok() || version != graph_asset_version || endian != little_endian_marker ||
      header_size != graph_header_size || file_size != bytes.size() ||
      payload_offset != graph_header_size || payload_size == 0 ||
      node_count == 0 || node_count > max_graph_nodes ||
      !region(payload_offset, payload_size, 1, bytes.size()) ||
      payload_offset + payload_size != bytes.size() ||
      crc32(bytes, graph_crc_offset) != stored_crc) {
    return make_unexpected(Error{ErrorCode::invalid_format, "graph asset bounds or integrity failure"});
  }
  std::string plan;
  plan.reserve(payload_size);
  for (std::size_t index = 0; index < payload_size; ++index) {
    const char character = static_cast<char>(std::to_integer<std::uint8_t>(bytes[payload_offset + index]));
    if (character == '\0')
      return make_unexpected(Error{ErrorCode::invalid_format, "graph plan contains a null byte"});
    plan.push_back(character);
  }
  if (!valid_graph_plan_json(plan, node_count))
    return make_unexpected(Error{ErrorCode::invalid_format, "graph plan is not canonical JSON"});
  return plan;
}

Expected<AssetSummary, Error> inspect_asset(std::span<const std::byte> bytes) {
  if (magic_equals(bytes, skeleton_magic)) {
    const auto decoded = decode_skeleton(bytes);
    if (!decoded) return make_unexpected(decoded.error());
    return AssetSummary{AssetKind::skeleton, skeleton_asset_version,
                        static_cast<std::uint32_t>(decoded->joints.size()), bytes.size()};
  }
  if (magic_equals(bytes, clip_magic)) {
    const auto decoded = decode_clip(bytes);
    if (!decoded) return make_unexpected(decoded.error());
    return AssetSummary{AssetKind::clip, clip_asset_version,
                        static_cast<std::uint32_t>(decoded->tracks.size()), bytes.size()};
  }
  if (magic_equals(bytes, graph_magic)) {
    const auto decoded = decode_graph_plan(bytes);
    if (!decoded) return make_unexpected(decoded.error());
    Reader header(bytes, 28);
    const auto node_count = header.u32();
    return AssetSummary{AssetKind::graph, graph_asset_version, node_count, bytes.size()};
  }
  return make_unexpected(Error{ErrorCode::invalid_format, "unknown asset magic"});
}

}  // namespace animgraph
