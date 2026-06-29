// Single-file converter for osu! beatmaps:
//   osz_to_mma <input.osz|input.osu> [--out output_dir] [--verbose]
//
// .osz files are zip packages. On Windows this converter uses the built-in
// PowerShell Expand-Archive command to extract them before parsing .osu files.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kBaseRows = 4;
constexpr int kBaseCols = 5;
constexpr int kMinRows = 1;
constexpr int kMaxRows = 4;
constexpr int kMinCols = 3;
constexpr int kMaxCols = 6;
constexpr int kActiveCols = 4;
constexpr int kFormationDurationMs = 500;
constexpr int kScrollDurationMs = 200;
constexpr int kDenseGapMs = 520;
constexpr int kRowStabilityLookaheadMs = 1000;
constexpr int kFormationCooldownMs = 4000;
constexpr int kFormationStabilityLookaheadMs = 2500;
constexpr int kMinStableTargetNotes = 3;
constexpr double kDefaultBlockSize = 0.9;

enum TransformType {
    MATRIX_TRANSFORM_NONE = 0,
    MATRIX_TRANSFORM_SCALE_ONLY = 100,
    MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP = 201,
    MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM = 202,
    MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH = 203,
    MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP = 204,
    MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM = 205,
    MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH = 206,
    MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT = 221,
    MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT = 222,
    MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH = 223,
    MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT = 224,
    MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT = 225,
    MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH = 226,
    MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX = 299,
    MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD = 301,
    MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE = 302,
    MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD = 303,
    MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE = 304,
    MATRIX_TRANSFORM_ROTATE_COMPLEX = 399,
};

struct Args {
    fs::path input;
    fs::path outDir;
    bool verbose = false;
};

struct TimingPoint {
    int64_t time = 0;
    double beatLength = 500.0;
    bool uninherited = true;
};

struct RawHitObject {
    int x = 0;
    int y = 0;
    int64_t time = 0;
    int type = 0;
    int64_t endTime = 0;
    double pixelLength = 0.0;
    int repeatCount = 1;
    char mmaType = 'T';
};

struct OsuBeatmap {
    fs::path sourcePath;
    std::string audioFilename = "audio.mp3";
    int64_t previewTime = -1;
    std::string title = "Unknown Title";
    std::string artist = "Unknown Artist";
    std::string creator = "Unknown Creator";
    std::string version = "Converted";
    double hp = 5.0;
    double od = 5.0;
    double ar = 5.0;
    double sliderMultiplier = 1.4;
    std::vector<TimingPoint> timingPoints;
    std::vector<RawHitObject> hitObjects;
};

struct NoteWindow {
    int64_t displayStart = 0;
    int64_t earliestHit = 0;
    int64_t latestHit = 0;
};

struct ConvertedNote {
    int x = 0;
    int y = 0;
    int64_t time = 0;
    int64_t endTime = 0;
    char type = 'T';
    NoteWindow window;
    NoteWindow releaseWindow;
    bool dropped = false;
};

struct Formation {
    int64_t time = 0;
    int rows = kBaseRows;
    int cols = kBaseCols;
    int transformType = MATRIX_TRANSFORM_NONE;
    int64_t durationMs = 0;
    double blockSize = 1.0;
    bool shortFormat = true;
};

struct MatrixShape {
    int rows = kBaseRows;
    int cols = kBaseCols;
};

std::string trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::string part;
    std::stringstream ss(value);
    while (std::getline(ss, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

bool startsWithBom(const std::string& text) {
    return text.size() >= 3 &&
           static_cast<unsigned char>(text[0]) == 0xEF &&
           static_cast<unsigned char>(text[1]) == 0xBB &&
           static_cast<unsigned char>(text[2]) == 0xBF;
}

std::string readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (startsWithBom(text)) {
        text.erase(0, 3);
    }
    return text;
}

void writeTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    out << text;
}

std::optional<int> parseInt(const std::string& value) {
    try {
        size_t used = 0;
        int result = std::stoi(trim(value), &used);
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int64_t> parseInt64(const std::string& value) {
    try {
        size_t used = 0;
        int64_t result = std::stoll(trim(value), &used);
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDouble(const std::string& value) {
    try {
        size_t used = 0;
        double result = std::stod(trim(value), &used);
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string sanitizeFilename(std::string value) {
    if (value.empty()) {
        return "converted";
    }
    const std::string illegal = "<>:\"/\\|?*";
    for (char& c : value) {
        if (illegal.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '.')) {
        value.pop_back();
    }
    return value.empty() ? "converted" : value;
}

std::string psQuote(const fs::path& path) {
    std::string value = path.string();
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

Args parseArgs(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error(
            "usage: osz_to_mma <input.osz|input.osu> [--out output_dir] [--verbose]");
    }

    Args args;
    args.input = fs::path(argv[1]);
    for (int i = 2; i < argc; ++i) {
        std::string token = argv[i];
        if (token == "--out" && i + 1 < argc) {
            args.outDir = fs::path(argv[++i]);
        } else if (token == "--verbose") {
            args.verbose = true;
        } else {
            throw std::runtime_error("unknown argument: " + token);
        }
    }

    if (!fs::exists(args.input)) {
        throw std::runtime_error("input does not exist: " + args.input.string());
    }

    if (args.outDir.empty()) {
        fs::path parent = args.input.parent_path();
        if (parent.empty()) {
            parent = fs::current_path();
        }
        args.outDir = parent / (args.input.stem().string() + "_mma");
    }
    return args;
}

std::vector<fs::path> collectOsuFilesFromInput(const Args& args) {
    std::vector<fs::path> osuFiles;
    const std::string ext = lowerAscii(args.input.extension().string());
    if (ext == ".osu") {
        osuFiles.push_back(args.input);
        return osuFiles;
    }

    if (ext != ".osz" && ext != ".zip") {
        throw std::runtime_error("input must be .osz, .zip, or .osu");
    }

    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    fs::path tempRoot = fs::temp_directory_path() / ("osz_to_mma_" + std::to_string(stamp));
    fs::path zipPath = tempRoot / "package.zip";
    fs::path extractDir = tempRoot / "extract";
    fs::create_directories(extractDir);
    fs::copy_file(args.input, zipPath, fs::copy_options::overwrite_existing);

    std::string command = "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"Expand-Archive -LiteralPath " + psQuote(zipPath) +
        " -DestinationPath " + psQuote(extractDir) + " -Force\"";
    int code = std::system(command.c_str());
    if (code != 0) {
        throw std::runtime_error("failed to extract .osz with PowerShell Expand-Archive");
    }

    for (const auto& entry : fs::recursive_directory_iterator(extractDir)) {
        if (entry.is_regular_file() && lowerAscii(entry.path().extension().string()) == ".osu") {
            osuFiles.push_back(entry.path());
        }
    }
    std::sort(osuFiles.begin(), osuFiles.end());
    if (osuFiles.empty()) {
        throw std::runtime_error("no .osu files found inside input archive");
    }
    return osuFiles;
}

double currentBeatLengthAt(const std::vector<TimingPoint>& points, int64_t time) {
    double beatLength = 500.0;
    for (const auto& point : points) {
        if (point.time > time) {
            break;
        }
        if (point.uninherited && point.beatLength > 0.0) {
            beatLength = point.beatLength;
        }
    }
    return beatLength;
}

double currentSliderVelocityAt(const std::vector<TimingPoint>& points, int64_t time) {
    double sliderVelocity = 1.0;
    for (const auto& point : points) {
        if (point.time > time) {
            break;
        }
        if (!point.uninherited && point.beatLength < 0.0) {
            sliderVelocity = std::max(0.1, -100.0 / point.beatLength);
        }
    }
    return sliderVelocity;
}

int64_t estimateSliderEndTime(const OsuBeatmap& map, const RawHitObject& object) {
    const double beatLength = currentBeatLengthAt(map.timingPoints, object.time);
    const double sliderVelocity = currentSliderVelocityAt(map.timingPoints, object.time);
    const double denominator = std::max(0.001, map.sliderMultiplier * 100.0 * sliderVelocity);
    const double beats = object.pixelLength * std::max(1, object.repeatCount) / denominator;
    const int64_t duration = static_cast<int64_t>(std::llround(beats * beatLength));
    return object.time + std::max<int64_t>(1, duration);
}

RawHitObject parseHitObjectLine(const std::string& line) {
    const auto parts = split(line, ',');
    if (parts.size() < 5) {
        throw std::runtime_error("invalid HitObject line: " + line);
    }

    RawHitObject object;
    object.x = parseInt(parts[0]).value_or(0);
    object.y = parseInt(parts[1]).value_or(0);
    object.time = parseInt64(parts[2]).value_or(0);
    object.type = parseInt(parts[3]).value_or(0);

    const bool isCircle = (object.type & 1) != 0;
    const bool isSlider = (object.type & 2) != 0;
    const bool isSpinner = (object.type & 8) != 0;

    if (isSlider) {
        object.mmaType = 'H';
        if (parts.size() >= 8) {
            object.repeatCount = std::max(1, parseInt(parts[6]).value_or(1));
            object.pixelLength = parseDouble(parts[7]).value_or(0.0);
        }
    } else if (isSpinner) {
        object.mmaType = 'H';
        if (parts.size() >= 6) {
            object.endTime = parseInt64(parts[5]).value_or(object.time + 1);
        }
    } else if (isCircle) {
        object.mmaType = 'T';
    } else {
        object.mmaType = 'T';
    }

    return object;
}

OsuBeatmap parseOsuText(const fs::path& path, const std::string& text) {
    OsuBeatmap map;
    map.sourcePath = path;

    std::string section;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        if (section == "HitObjects") {
            try {
                map.hitObjects.push_back(parseHitObjectLine(line));
            } catch (const std::exception& e) {
                std::cerr << "warning: " << e.what() << "\n";
            }
            continue;
        }

        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (section == "TimingPoints") {
                const auto parts = split(line, ',');
                if (parts.size() >= 2) {
                    TimingPoint point;
                    point.time = parseInt64(parts[0]).value_or(0);
                    point.beatLength = parseDouble(parts[1]).value_or(500.0);
                    if (parts.size() >= 7) {
                        point.uninherited = parseInt(parts[6]).value_or(1) != 0;
                    } else {
                        point.uninherited = point.beatLength > 0.0;
                    }
                    map.timingPoints.push_back(point);
                }
            }
            continue;
        }

        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));

        if (section == "General") {
            if (key == "AudioFilename") map.audioFilename = value;
            if (key == "PreviewTime") map.previewTime = parseInt64(value).value_or(-1);
        } else if (section == "Metadata") {
            if (key == "Title" || key == "TitleUnicode") map.title = value.empty() ? map.title : value;
            if (key == "Artist" || key == "ArtistUnicode") map.artist = value.empty() ? map.artist : value;
            if (key == "Creator") map.creator = value;
            if (key == "Version") map.version = value;
        } else if (section == "Difficulty") {
            if (key == "HPDrainRate") map.hp = parseDouble(value).value_or(map.hp);
            if (key == "OverallDifficulty") map.od = parseDouble(value).value_or(map.od);
            if (key == "ApproachRate") map.ar = parseDouble(value).value_or(map.ar);
            if (key == "SliderMultiplier") {
                map.sliderMultiplier = parseDouble(value).value_or(map.sliderMultiplier);
            }
        }
    }

    if (map.timingPoints.empty()) {
        map.timingPoints.push_back(TimingPoint{0, 500.0, true});
    }
    std::sort(map.timingPoints.begin(), map.timingPoints.end(), [](const auto& a, const auto& b) {
        return a.time < b.time;
    });
    std::sort(map.hitObjects.begin(), map.hitObjects.end(), [](const auto& a, const auto& b) {
        return a.time < b.time;
    });
    return map;
}

NoteWindow makeWindow(int64_t time, double ar, double od) {
    const double approachMs = std::max(0.0, 1800.0 - ar * 120.0);
    const double goodW = std::max(0.0, 65.0 - 2.6 * od);
    const double missW = goodW + 50.0;

    NoteWindow window;
    window.displayStart = time - static_cast<int64_t>(std::llround(approachMs));
    window.earliestHit = time - static_cast<int64_t>(std::llround(missW));
    window.latestHit = time + static_cast<int64_t>(std::llround(missW));
    return window;
}

std::vector<ConvertedNote> makeConvertedNotes(const OsuBeatmap& map) {
    std::vector<ConvertedNote> notes;
    notes.reserve(map.hitObjects.size());
    for (const auto& object : map.hitObjects) {
        ConvertedNote note;
        note.x = object.x;
        note.y = object.y;
        note.time = object.time;
        note.type = object.mmaType;
        note.endTime = object.endTime;
        if (note.type == 'H' && note.endTime <= note.time) {
            if ((object.type & 2) != 0) {
                note.endTime = estimateSliderEndTime(map, object);
            } else {
                note.endTime = note.time + 1;
            }
        }
        note.window = makeWindow(note.time, map.ar, map.od);
        note.releaseWindow = makeWindow(note.type == 'H' ? note.endTime : note.time, map.ar, map.od);
        notes.push_back(note);
    }
    return notes;
}

int clampInt(int value, int lo, int hi) {
    return std::max(lo, std::min(hi, value));
}

MatrixShape targetShapeForDensity(const std::vector<ConvertedNote>& notes, size_t index) {
    const int64_t center = notes[index].time;
    const int64_t start = center - 1500;
    const int64_t end = center + 1500;
    int count = 0;
    for (const auto& note : notes) {
        if (!note.dropped && note.time >= start && note.time <= end) {
            ++count;
        }
    }

    const double dps = count / 3.0;
    MatrixShape shape;
    if (dps < 1.5) {
        shape.rows = 2;
        shape.cols = 3;
    } else if (dps < 2.5) {
        shape.rows = 3;
        shape.cols = 4;
    } else if (dps < 4.0) {
        shape.rows = 4;
        shape.cols = 5;
    } else {
        shape.rows = 4;
        shape.cols = 6;
    }
    shape.rows = clampInt(shape.rows, kMinRows, kMaxRows);
    shape.cols = clampInt(shape.cols, kMinCols, kMaxCols);
    return shape;
}

int transformTypeFor(const MatrixShape& from, const MatrixShape& to) {
    const int rowDelta = to.rows - from.rows;
    const int colDelta = to.cols - from.cols;
    if (rowDelta == 0 && colDelta == 0) {
        return MATRIX_TRANSFORM_NONE;
    }
    if (rowDelta > 0 && colDelta == 0) {
        return rowDelta > 1 ? MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH : MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM;
    }
    if (rowDelta < 0 && colDelta == 0) {
        return rowDelta < -1 ? MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH : MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM;
    }
    if (rowDelta == 0 && colDelta > 0) {
        return colDelta > 1 ? MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH : MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT;
    }
    if (rowDelta == 0 && colDelta < 0) {
        return colDelta < -1 ? MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH : MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT;
    }
    if (rowDelta > 0 && colDelta > 0) {
        return MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD;
    }
    if (rowDelta > 0 && colDelta < 0) {
        return MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE;
    }
    if (rowDelta < 0 && colDelta > 0) {
        return MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD;
    }
    if (rowDelta < 0 && colDelta < 0) {
        return MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE;
    }
    return MATRIX_TRANSFORM_ROTATE_COMPLEX;
}

int mappedColForShape(int x, int cols) {
    const double clampedX = std::max(0.0, std::min(511.999, static_cast<double>(x)));
    return clampInt(static_cast<int>(std::floor(clampedX / 512.0 * cols)), 0, cols - 1);
}

int mappedRowForShape(int y, int rows) {
    const double clampedY = std::max(0.0, std::min(383.999, static_cast<double>(y)));
    return clampInt(static_cast<int>(std::floor(clampedY / 384.0 * rows)), 0, rows - 1);
}

int previousKeptIndex(const std::vector<ConvertedNote>& notes, size_t before) {
    for (size_t i = before; i > 0; --i) {
        const size_t index = i - 1;
        if (!notes[index].dropped) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int nextKeptIndex(const std::vector<ConvertedNote>& notes, size_t after) {
    for (size_t i = after + 1; i < notes.size(); ++i) {
        if (!notes[i].dropped) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int64_t blockingLatestHit(const ConvertedNote& note) {
    return note.type == 'H' ? note.releaseWindow.latestHit : note.window.latestHit;
}

bool isDenseRhythmAround(const std::vector<ConvertedNote>& notes, size_t index) {
    const int prevIndex = previousKeptIndex(notes, index);
    if (prevIndex >= 0 && notes[index].time - notes[prevIndex].time <= kDenseGapMs) {
        return true;
    }

    const int nextIndex = nextKeptIndex(notes, index);
    if (nextIndex >= 0 && notes[nextIndex].time - notes[index].time <= kDenseGapMs) {
        return true;
    }

    return false;
}

bool keepsRowsStableNearTransition(
    const std::vector<ConvertedNote>& notes,
    size_t index,
    const MatrixShape& current,
    const MatrixShape& target) {

    if (current.rows == target.rows) {
        return true;
    }

    const int64_t end = notes[index].time + kRowStabilityLookaheadMs;
    for (size_t i = index; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }
        if (notes[i].time > end) {
            break;
        }

        const int rowBefore = mappedRowForShape(notes[i].y, current.rows);
        const int rowAfter = mappedRowForShape(notes[i].y, target.rows);
        if (rowBefore != rowAfter) {
            return false;
        }
    }

    return true;
}

bool hasStableFormationTarget(
    const std::vector<ConvertedNote>& notes,
    size_t index,
    const MatrixShape& current,
    const MatrixShape& target) {

    if (target.rows == current.rows && target.cols == current.cols) {
        return false;
    }

    const int64_t end = notes[index].time + kFormationStabilityLookaheadMs;
    int considered = 0;
    int targetVotes = 0;
    for (size_t i = index; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }
        if (notes[i].time > end) {
            break;
        }
        if (isDenseRhythmAround(notes, i)) {
            return false;
        }

        const MatrixShape candidate = targetShapeForDensity(notes, i);
        ++considered;
        if (candidate.rows == target.rows && candidate.cols == target.cols) {
            ++targetVotes;
        }
    }

    return considered >= kMinStableTargetNotes && targetVotes * 2 >= considered + 1;
}

bool scheduleTransitionBefore(
    std::vector<ConvertedNote>& notes,
    size_t noteIndex,
    int64_t durationMs,
    bool includesFormation,
    int64_t lastTransitionEnd,
    int maxDrops,
    int64_t& startMs) {

    const int64_t endLimit = includesFormation
        ? notes[noteIndex].window.displayStart
        : notes[noteIndex].window.earliestHit;
    std::vector<size_t> droppedForAttempt;

    while (true) {
        const int prevIndex = previousKeptIndex(notes, noteIndex);
        const int64_t safeAfter = prevIndex >= 0 ? blockingLatestHit(notes[prevIndex]) : 0;
        const int64_t earliestStart = std::max(safeAfter, lastTransitionEnd);
        if (endLimit - earliestStart >= durationMs) {
            startMs = std::max(earliestStart, endLimit - durationMs);
            return true;
        }

        if (prevIndex >= 0 &&
            maxDrops > 0 &&
            notes[prevIndex].type != 'H' &&
            !isDenseRhythmAround(notes, static_cast<size_t>(prevIndex))) {
            notes[prevIndex].dropped = true;
            droppedForAttempt.push_back(static_cast<size_t>(prevIndex));
            --maxDrops;
            continue;
        }

        if (endLimit >= durationMs && lastTransitionEnd <= endLimit - durationMs) {
            startMs = std::max<int64_t>(lastTransitionEnd, endLimit - durationMs);
            return true;
        }
        for (size_t index : droppedForAttempt) {
            notes[index].dropped = false;
        }
        return false;
    }
}

std::vector<Formation> generateFormationsAndFilter(std::vector<ConvertedNote>& notes) {
    std::vector<Formation> formations;
    formations.push_back(Formation{0, kBaseRows, kBaseCols, MATRIX_TRANSFORM_NONE, 0, 1.0, true});

    MatrixShape current{kBaseRows, kBaseCols};
    int activeStart = 0;
    int64_t lastTransitionEnd = 0;
    int64_t lastFormationTime = 0;

    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].dropped) {
            continue;
        }

        MatrixShape target = targetShapeForDensity(notes, i);
        const bool denseRhythm = isDenseRhythmAround(notes, i);
        bool needsFormation = target.rows != current.rows || target.cols != current.cols;
        if (needsFormation && (
                denseRhythm ||
                notes[i].time - lastFormationTime < kFormationCooldownMs ||
                !hasStableFormationTarget(notes, i, current, target) ||
                !keepsRowsStableNearTransition(notes, i, current, target))) {
            target = current;
            needsFormation = false;
        }

        const int maxActiveStart = std::max(0, target.cols - kActiveCols);
        activeStart = clampInt(activeStart, 0, maxActiveStart);
        const int targetCol = mappedColForShape(notes[i].x, target.cols);
        bool needsScroll = false;
        int nextActiveStart = activeStart;
        if (needsFormation) {
            if (target.cols > kActiveCols) {
                nextActiveStart = clampInt(targetCol - kActiveCols / 2, 0, maxActiveStart);
            } else {
                nextActiveStart = 0;
            }
        } else if (!denseRhythm && target.cols > kActiveCols) {
            if (targetCol < activeStart) {
                needsScroll = true;
                nextActiveStart = clampInt(targetCol, 0, maxActiveStart);
            } else if (targetCol >= activeStart + kActiveCols) {
                needsScroll = true;
                nextActiveStart = clampInt(targetCol - kActiveCols + 1, 0, maxActiveStart);
            }
        }

        if (!needsFormation && !needsScroll) {
            continue;
        }

        const int64_t duration = std::max(
            needsFormation ? static_cast<int64_t>(kFormationDurationMs) : 0,
            needsScroll ? static_cast<int64_t>(kScrollDurationMs) : 0);

        int64_t transitionStart = 0;
        const int maxDrops = needsFormation ? 1 : 0;
        if (!scheduleTransitionBefore(
                notes,
                i,
                duration,
                needsFormation,
                lastTransitionEnd,
                maxDrops,
                transitionStart)) {
            continue;
        }

        lastTransitionEnd = transitionStart + duration;
        activeStart = nextActiveStart;
        if (needsFormation) {
            const int type = transformTypeFor(current, target);
            formations.push_back(Formation{
                transitionStart,
                target.rows,
                target.cols,
                type,
                duration,
                kDefaultBlockSize,
                false,
            });
            current = target;
            lastFormationTime = transitionStart;
        }
    }

    std::sort(formations.begin(), formations.end(), [](const Formation& a, const Formation& b) {
        return a.time < b.time;
    });
    return formations;
}

MatrixShape shapeAtTime(const std::vector<Formation>& formations, int64_t time) {
    MatrixShape shape{kBaseRows, kBaseCols};
    for (const auto& formation : formations) {
        if (formation.time > time) {
            break;
        }
        shape.rows = formation.rows;
        shape.cols = formation.cols;
    }
    return shape;
}

std::string formatDouble(double value) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << value;
    std::string text = ss.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

std::string macroSection() {
    return
        "[FormationTransformMacros]\n"
        "MATRIX_TRANSFORM_NONE=0\n"
        "MATRIX_TRANSFORM_SCALE_ONLY=100\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_ADD_TOP=201\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTTOM=202\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_ADD_BOTH=203\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_TOP=204\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTTOM=205\n"
        "MATRIX_TRANSFORM_SLIDE_ROW_REMOVE_BOTH=206\n"
        "MATRIX_TRANSFORM_SLIDE_COL_ADD_LEFT=221\n"
        "MATRIX_TRANSFORM_SLIDE_COL_ADD_RIGHT=222\n"
        "MATRIX_TRANSFORM_SLIDE_COL_ADD_BOTH=223\n"
        "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_LEFT=224\n"
        "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_RIGHT=225\n"
        "MATRIX_TRANSFORM_SLIDE_COL_REMOVE_BOTH=226\n"
        "MATRIX_TRANSFORM_SLIDE_SINGLE_AXIS_COMPLEX=299\n"
        "MATRIX_TRANSFORM_ROTATE_ROWS_COLS_ADD=301\n"
        "MATRIX_TRANSFORM_ROTATE_ROWS_ADD_COLS_REMOVE=302\n"
        "MATRIX_TRANSFORM_ROTATE_ROWS_REMOVE_COLS_ADD=303\n"
        "MATRIX_TRANSFORM_ROTATE_ROWS_COLS_REMOVE=304\n"
        "MATRIX_TRANSFORM_ROTATE_COMPLEX=399\n";
}

std::string buildMmaText(const OsuBeatmap& map, const std::vector<ConvertedNote>& notes,
                         const std::vector<Formation>& formations) {
    std::ostringstream out;
    out << "MMA2\n";
    out << "# converted_from=" << map.sourcePath.filename().string() << "\n\n";

    out << "[General]\n";
    out << "AudioFilename=" << map.audioFilename << "\n";
    out << "PreviewTime=" << map.previewTime << "\n\n";

    out << "[Meta]\n";
    out << "Title=" << map.title << "\n";
    out << "Artist=" << map.artist << "\n";
    out << "Creator=" << map.creator << "\n";
    out << "Version=" << map.version << "\n\n";

    out << "[Difficulty]\n";
    out << "HP=" << formatDouble(map.hp) << "\n";
    out << "OD=" << formatDouble(map.od) << "\n";
    out << "AR=" << formatDouble(map.ar) << "\n\n";

    out << macroSection() << "\n";

    out << "[Formations]\n";
    for (const auto& formation : formations) {
        if (formation.shortFormat) {
            out << formation.time << "," << formation.rows << "," << formation.cols << "\n";
        } else {
            out << formation.time << "," << formation.rows << "," << formation.cols << ","
                << formation.transformType << "," << formation.durationMs << ","
                << formatDouble(formation.blockSize) << "\n";
        }
    }
    out << "\n";

    out << "[Notes]\n";
    for (const auto& note : notes) {
        if (note.dropped) {
            continue;
        }
        const MatrixShape shape = shapeAtTime(formations, note.time);
        const int row = mappedRowForShape(note.y, shape.rows);
        const int col = mappedColForShape(note.x, shape.cols);
        out << note.time << "," << row << "," << col << "," << note.type;
        if (note.type == 'H') {
            out << "," << std::max<int64_t>(note.time + 1, note.endTime);
        }
        out << "\n";
    }

    return out.str();
}

fs::path outputPathFor(const fs::path& outDir, const OsuBeatmap& map) {
    std::string name = map.artist + " - " + map.title + " [" + map.version + "].mma";
    return outDir / fs::u8path(sanitizeFilename(name));
}

void convertOne(const fs::path& osuPath, const fs::path& outDir, bool verbose) {
    const std::string text = readTextFile(osuPath);
    OsuBeatmap map = parseOsuText(osuPath, text);
    std::vector<ConvertedNote> notes = makeConvertedNotes(map);
    std::vector<Formation> formations = generateFormationsAndFilter(notes);
    const std::string mma = buildMmaText(map, notes, formations);
    const fs::path outPath = outputPathFor(outDir, map);
    writeTextFile(outPath, mma);

    if (verbose) {
        const auto kept = std::count_if(notes.begin(), notes.end(), [](const auto& note) {
            return !note.dropped;
        });
        std::cerr << "converted " << osuPath.filename().string()
                  << " -> " << outPath.string()
                  << " notes=" << kept << "/" << notes.size()
                  << " formations=" << formations.size() << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parseArgs(argc, argv);
        fs::create_directories(args.outDir);
        const std::vector<fs::path> osuFiles = collectOsuFilesFromInput(args);
        for (const auto& osuPath : osuFiles) {
            convertOne(osuPath, args.outDir, args.verbose);
        }
        std::cout << "Converted " << osuFiles.size() << " .osu file(s) to "
                  << args.outDir.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
