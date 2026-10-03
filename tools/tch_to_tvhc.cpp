#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr char kSignature[] = "$*$* &&&&$*$";
constexpr std::uint32_t kUnmappedContext = 0xFFFF;
constexpr std::uint32_t kTvMaxContext = 16379;
constexpr std::uint32_t kContentsContext = 10030;

// CP437's printable upper half, used to transcode THELP bytes to the UTF-8
// source accepted by this Turbo Vision port. Format notes: github.com/mariolacko/THelpViewer/docs/Formats/Borland%20THELP.md
constexpr char32_t kCp437Upper[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
    0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
    0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
    0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
    0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
    0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
    0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
    0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
    0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0
};

constexpr char32_t kCp437Controls[32] = {
    0,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,
    0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,
    0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC
};

void appendUtf8(std::string &out, char32_t cp) {
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string fromCp437(const std::string &bytes) {
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        if (c == 0) continue;
        appendUtf8(result, c < 32 ? kCp437Controls[c] : c == 127 ? 0x2302 : c < 128 ? c : kCp437Upper[c - 128]);
    }
    return result;
}

struct Topic {
    std::uint32_t id = kUnmappedContext;
    std::vector<std::uint32_t> aliases;
    std::vector<std::string> lines;
    std::vector<std::uint32_t> links;
    std::uint32_t previous = 0, next = 0;
};

struct IndexEntry { std::string label; std::uint32_t context = 0; };

class Reader {
public:
    explicit Reader(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

    void parse() {
        const std::string stamp = readStringZ();
        if (stamp != "TURBO C HELP FILE.") throw std::runtime_error("not a Turbo C THELP file");
        if (byte() != 0x1A || readStringZ() != kSignature) throw std::runtime_error("invalid THELP signature");
        version_ = byte();
        const auto textVersion = byte();
        if (version_ != 52 || textVersion != 1) throw std::runtime_error("converter supports THELP format 52 only");

        while (pos_ < data_.size()) {
            const auto start = pos_;
            const auto type = byte();
            const auto length = word();
            const auto end = start + 3u + length;
            if (end > data_.size()) throw std::runtime_error("record extends beyond end of file");
            switch (type) {
                case 0: parseHeader(length); break;
                case 1: parseContexts(end); break;
                case 2: parseText(start, end); break;
                case 3: parseKeywords(end); break;
                case 4: parseIndex(end); break;
                case 5: parseCompression(end); break;
                case 6: pos_ = end; break;
                default: pos_ = end; break;
            }
            if (pos_ > end) throw std::runtime_error("record parser overran record");
            pos_ = end;
        }
        if (topics_.empty() || contexts_.empty()) throw std::runtime_error("missing topic/context records");
    }

    const std::vector<Topic> &topics() const { return topics_; }
    const std::vector<IndexEntry> &index() const { return index_; }
    std::uint32_t mainIndex() const { return mainIndex_; }
    std::uint32_t contextCount() const { return static_cast<std::uint32_t>(contexts_.size()); }

private:
    std::uint8_t byte() {
        if (pos_ >= data_.size()) throw std::runtime_error("unexpected end of file");
        return data_[pos_++];
    }
    std::uint16_t word() { const auto lo = byte(); return static_cast<std::uint16_t>(lo | (byte() << 8)); }
    std::string readStringZ() {
        std::string s;
        while (pos_ < data_.size() && data_[pos_] != 0) s.push_back(static_cast<char>(data_[pos_++]));
        if (pos_ == data_.size()) throw std::runtime_error("unterminated header string");
        ++pos_;
        return s;
    }
    void requireRecordBytes(std::size_t end, std::size_t count) {
        if (pos_ + count > end) throw std::runtime_error("truncated THELP record");
    }
    void parseHeader(std::size_t length) {
        if (length < 9) throw std::runtime_error("short format-52 file header");
        const auto options = word();
        mainIndex_ = word();
        (void)word();
        height_ = byte(); width_ = byte(); leftMargin_ = byte();
        (void)options;
    }
    void parseContexts(std::size_t end) {
        const auto count = word();
        if (pos_ + static_cast<std::size_t>(count) * 3 > end) throw std::runtime_error("truncated context table");
        contexts_.clear(); contexts_.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            const auto b0 = byte(), b1 = byte(), b2 = byte();
            contexts_.push_back(static_cast<std::uint32_t>(b0 | (b1 << 8) | (b2 << 16)));
        }
    }
    void parseCompression(std::size_t end) {
        requireRecordBytes(end, 15);
        compressionType_ = byte();
        if (compressionType_ != 2) throw std::runtime_error("unsupported THELP compression type");
        for (auto &c : charTable_) c = byte();
        hasCompression_ = true;
    }
    std::uint32_t contextAt(std::size_t fileOffset) const {
        for (std::size_t i = 0; i < contexts_.size(); ++i)
            if (contexts_[i] == fileOffset) return static_cast<std::uint32_t>(i);
        return kUnmappedContext;
    }
    void parseText(std::size_t recordStart, std::size_t end) {
        if (!hasCompression_) throw std::runtime_error("missing compression record");
        Topic topic;
        topic.id = contextAt(recordStart);
        for (std::size_t i = 0; i < contexts_.size(); ++i)
            if (contexts_[i] == recordStart && i != topic.id) topic.aliases.push_back(static_cast<std::uint32_t>(i));

        std::vector<std::uint8_t> nibbles;
        nibbles.reserve((end - pos_) * 2);
        while (pos_ < end) {
            const auto b = byte();
            nibbles.push_back(b & 0x0F);
            nibbles.push_back(b >> 4);
        }
        std::size_t ni = 0;
        std::string line;
        auto nextNibble = [&]() -> std::uint8_t {
            if (ni >= nibbles.size()) throw std::runtime_error("truncated nibble-compressed text");
            return nibbles[ni++];
        };
        auto emitLine = [&]() { topic.lines.push_back(line); line.clear(); };
        while (ni < nibbles.size()) {
            const auto n = nextNibble();
            if (n == 0) { emitLine(); continue; }
            if (n <= 0x0D) { line.push_back(static_cast<char>(charTable_[n])); continue; }
            if (n == 0x0E) {
                const auto count = static_cast<unsigned>(nextNibble()) + 2;
                const auto repeated = nextNibble();
                std::uint8_t value;
                if (repeated == 0) { for (unsigned j = 0; j < count; ++j) emitLine(); continue; }
                if (repeated <= 0x0D) value = charTable_[repeated];
                else if (repeated == 0x0F) value = static_cast<std::uint8_t>(nextNibble() | (nextNibble() << 4));
                else throw std::runtime_error("invalid repeated nibble code");
                line.append(count, static_cast<char>(value));
                continue;
            }
            const auto value = static_cast<std::uint8_t>(nextNibble() | (nextNibble() << 4));
            if (value == 1) break;
            line.push_back(static_cast<char>(value));
        }
        if (!line.empty()) emitLine();
        topics_.push_back(std::move(topic));
    }
    void parseKeywords(std::size_t end) {
        if (topics_.empty()) throw std::runtime_error("keyword record without topic");
        const auto up = word(); const auto down = word();
        topics_.back().previous = up;
        topics_.back().next = down;
        const auto count = word();
        if (pos_ + static_cast<std::size_t>(count) * 2 > end) throw std::runtime_error("truncated keyword record");
        auto &links = topics_.back().links;
        links.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) links.push_back(word());
    }
    void parseIndex(std::size_t end) {
        const auto count = word();
        std::string previous;
        index_.clear(); index_.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            requireRecordBytes(end, 1);
            const auto header = byte();
            const auto keep = static_cast<std::size_t>((header >> 5) & 7);
            const auto add = static_cast<std::size_t>(header & 31);
            if (keep > previous.size()) throw std::runtime_error("invalid compressed index prefix");
            requireRecordBytes(end, add + 2);
            std::string label = previous.substr(0, keep);
            for (std::size_t j = 0; j < add; ++j) label.push_back(static_cast<char>(byte()));
            const auto context = word();
            index_.push_back({label, context});
            previous = std::move(label);
        }
    }

    std::vector<std::uint8_t> data_;
    std::size_t pos_ = 0;
    std::uint8_t version_ = 0, compressionType_ = 0, height_ = 0, width_ = 0, leftMargin_ = 0;
    bool hasCompression_ = false;
    std::uint32_t mainIndex_ = 0;
    std::uint8_t charTable_[14]{};
    std::vector<std::uint32_t> contexts_;
    std::vector<Topic> topics_;
    std::vector<IndexEntry> index_;
};

std::string symbol(std::uint32_t id) { return "Tch" + std::to_string(id); }

std::string escapePlain(const std::string &utf8) {
    std::string result;
    for (char c : utf8) {
        if (c == '{') result += "{{";
        else result.push_back(c);
    }
    return result;
}

std::string escapeLink(const std::string &utf8) {
    std::string result;
    for (char c : utf8) {
        if (c == '{' || c == ':' || c == '}') result.push_back(c);
        result.push_back(c);
    }
    return result;
}

std::string bucketFor(const std::string &label) {
    if (label.empty()) return "Symbols";
    unsigned char c = static_cast<unsigned char>(label.front());
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return std::string(1, static_cast<char>(c));
    return "Symbols";
}

struct OutputTopic { std::uint32_t id; const Topic *source; bool appendIndexLink = false; };

std::vector<std::uint8_t> readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input: " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeSource(const Reader &reader, const fs::path &output) {
    const auto &topics = reader.topics();
    std::map<std::uint32_t, std::uint32_t> mapped;
    std::uint32_t next = 1;
    for (const auto &topic : topics) {
        if (topic.id != kUnmappedContext) next = std::max(next, topic.id + 1);
        for (auto alias : topic.aliases) next = std::max(next, alias + 1);
    }
    const std::uint32_t remappedOne = next++;
    const std::uint32_t unmapped = next++;
    if (next > kContentsContext)
        throw std::runtime_error("source context IDs overlap the reserved Contents context");
    for (const auto &topic : topics) {
        mapped[topic.id] = topic.id == 1 ? remappedOne : topic.id == kUnmappedContext ? unmapped : topic.id;
        for (auto alias : topic.aliases) mapped[alias] = alias == 1 ? remappedOne : alias;
    }

    std::vector<std::string> buckets;
    std::map<std::string, std::vector<IndexEntry>> entriesByBucket;
    for (const auto &entry : reader.index()) {
        auto name = bucketFor(entry.label);
        if (!entriesByBucket.count(name)) buckets.push_back(name);
        entriesByBucket[name].push_back(entry);
    }
    const std::uint32_t indexRoot = kContentsContext + 1;
    next = indexRoot + 1;
    std::map<std::string, std::uint32_t> indexPages;
    for (const auto &name : buckets) indexPages[name] = next++;
    if (next > kTvMaxContext) throw std::runtime_error("generated topic IDs exceed TVHC's 16-bit topic limit");

    std::map<std::uint32_t, const Topic *> topicById;
    for (const auto &topic : topics) {
        topicById[mapped.at(topic.id)] = &topic;
        for (auto alias : topic.aliases) topicById[mapped.at(alias)] = &topic;
    }
    const auto contents = std::find_if(topics.begin(), topics.end(), [&](const Topic &t) { return t.id == reader.mainIndex(); });
    if (contents == topics.end()) throw std::runtime_error("main index context does not identify a topic");

    std::ofstream out(output, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create output: " + output.string());
    out << "; TurboIDE conversion of Borland THELP v52; source text encoded as UTF-8.\n"
        << "; Format reference: https://github.com/mariolacko/THelpViewer/blob/main/docs/Formats/Borland%20THELP.md\n";

    std::size_t emittedTopicCount = 0, linkCount = 0, unresolved = 0, codeControls = 0, navigationLinks = 0;
    auto emitTopic = [&](std::uint32_t id, const Topic &topic, bool indexEntry) {
        out << ".topic " << symbol(id) << "=" << id << "\n";
        if (indexEntry) {
            out << " Turbo C Help Index\n  -----------------\n";
            for (const auto &name : buckets)
                out << " " << "{" << name << ":" << symbol(indexPages.at(name)) << "}\n";
            linkCount += buckets.size();
            ++emittedTopicCount;
            return;
        }

        std::size_t linkIndex = 0;
        for (const auto &raw : topic.lines) {
            std::string filtered;
            std::size_t column = 0;
            bool hadCodeControl = false;
            for (unsigned char c : raw) {
                if (c == 0x05 || c == 0x10) { ++codeControls; hadCodeControl = true; continue; }
                if (c == '\t') {
                    const auto spaces = 8 - (column % 8);
                    filtered.append(spaces, ' ');
                    column += spaces;
                } else {
                    filtered.push_back(static_cast<char>(c));
                    ++column;
                }
            }
            if (hadCodeControl && std::all_of(filtered.begin(), filtered.end(), [](unsigned char c) { return c == ' '; })) continue;

            std::string line, plain, label;
            bool inLink = false;
            auto flushPlain = [&]() {
                if (!plain.empty()) { line += escapePlain(fromCp437(plain)); plain.clear(); }
            };
            for (unsigned char c : filtered) {
                if (c == 0x02) {
                    if (!inLink) {
                        flushPlain();
                        label.clear(); inLink = true;
                    } else {
                        const auto decoded = fromCp437(label);
                        if (linkIndex < topic.links.size()) {
                            const auto target = topic.links[linkIndex++];
                            ++linkCount;
                            const auto mappedTarget = mapped.find(target);
                            if (mappedTarget != mapped.end() && topicById.count(mappedTarget->second))
                                line += "{" + escapeLink(decoded) + ":" + symbol(mappedTarget->second) + "}";
                            else { line += escapePlain(decoded); ++unresolved; }
                        } else { line += escapePlain(decoded); ++unresolved; }
                        label.clear(); inLink = false;
                    }
                    continue;
                }
                (inLink ? label : plain).push_back(static_cast<char>(c));
            }
            if (inLink) { line += escapePlain(fromCp437(label)); ++unresolved; }
            else flushPlain();
            line = (line.empty() || line.front() != ' ') ? " " + line : line;
            if (line.size() > 254) throw std::runtime_error("TVHC input line exceeds its 255-byte limit at context " + std::to_string(id));
            out << line << "\n";
        }
        for (; linkIndex < topic.links.size(); ++linkIndex) { ++linkCount; ++unresolved; }
        auto emitNavigation = [&](const char *label, std::uint32_t context) {
            if (!context) return;
            ++navigationLinks;
            ++linkCount;
            const auto target = mapped.find(context);
            out << " " << label << ": ";
            if (target != mapped.end() && topicById.count(target->second)) out << "{" << label << ":" << symbol(target->second) << "}";
            else { out << label; ++unresolved; }
            out << "\n";
        };
        emitNavigation("Previous", topic.previous);
        emitNavigation("Next", topic.next);
        if (&topic == &*contents)
            out << " {Alphabetical index:" << symbol(indexRoot) << "}\n";
        ++emittedTopicCount;
    };

    // Preserve each screen and every arbitrary alias context. TVHC's source
    // aliases auto-number, so alias contexts become byte-identical topic bodies.
    for (const auto &topic : topics) {
        emitTopic(mapped.at(topic.id), topic, false);
        for (auto alias : topic.aliases) emitTopic(mapped.at(alias), topic, false);
    }
    // Keep a stable IDE entry point and the Borland Contents context.
    emitTopic(kContentsContext, *contents, false);
    emitTopic(indexRoot, *contents, true);
    for (const auto &name : buckets) {
        Topic indexTopic;
        indexTopic.id = indexPages.at(name);
        out << ".topic " << symbol(indexTopic.id) << "=" << indexTopic.id << "\n"
            << " " << name << "\n  " << std::string(name.size(), '-') << "\n";
        for (const auto &entry : entriesByBucket[name]) {
            const auto target = mapped.find(entry.context);
            const auto label = fromCp437(entry.label);
            std::string rendered = std::string(" {") + escapeLink(label) + ":";
            if (target != mapped.end() && topicById.count(target->second)) rendered += symbol(target->second) + "}";
            else { rendered = " " + escapePlain(label); ++unresolved; }
            if (rendered.size() > 254) throw std::runtime_error("TVHC index line exceeds its 255-byte limit");
            out << rendered << "\n";
            ++linkCount;
        }
        ++emittedTopicCount;
    }
    out.flush();
    if (!out) throw std::runtime_error("failed writing output source");
    std::cout << "format=THELP-v52 source_topics=" << topics.size()
              << " alias_contexts=" << std::accumulate(topics.begin(), topics.end(), std::size_t{0}, [](auto n, const Topic &t) { return n + t.aliases.size(); })
              << " index_entries=" << reader.index().size() << " source_contexts=" << reader.contextCount()
              << " emitted_topics=" << emittedTopicCount << " links=" << linkCount
              << " unresolved_links=" << unresolved << " page_navigation_links=" << navigationLinks << " code_control_bytes=" << codeControls
              << " contents_context=" << kContentsContext << " legacy_contents_context=" << contents->id << "\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: tch_to_tvhc <input.TCH> <output.txt>\n";
        return 2;
    }
    try {
        Reader reader(readFile(fs::u8path(argv[1])));
        reader.parse();
        writeSource(reader, fs::u8path(argv[2]));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "tch_to_tvhc: " << e.what() << "\n";
        return 1;
    }
}
