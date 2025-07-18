#ifndef slic3r_TextConfiguration_hpp_
#define slic3r_TextConfiguration_hpp_

#include <vector>
#include <string>
#include <optional>
#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>
#include "Point.hpp" // Transform3d

namespace Slic3r {
/// <summary>
/// User modifiable property of text style
/// NOTE: OnEdit fix serializations: EmbossStylesSerializable, TextConfigurationSerialization
/// </summary>
struct FontProp
{
    // define extra space between letters, negative mean closer letter
    // When not set value is zero and is not stored
    std::optional<int> char_gap; // [in font point]

    // define extra space between lines, negative mean closer lines
    // When not set value is zero and is not stored
    std::optional<int> line_gap; // [in font point]

    // positive value mean wider character shape
    // negative value mean tiner character shape
    // When not set value is zero and is not stored
    std::optional<float> boldness; // [in mm]

    // positive value mean italic of character (CW)
    // negative value mean CCW skew (unItalic)
    // When not set value is zero and is not stored
    std::optional<float> skew; // [ration x:y]

    // Parameter for True Type Font collections
    // Select index of font in collection
    std::optional<unsigned int> collection_number;

    // Distiguish projection per glyph
    bool per_glyph;

    // NOTE: way of serialize to 3mf force that zero must be default value
    enum class HorizontalAlign { left = 0, center, right };
    enum class VerticalAlign { top = 0, center, bottom };
    using Align = std::pair<HorizontalAlign, VerticalAlign>;
    // change pivot of text
    // When not set, center is used and is not stored
    Align align = Align(HorizontalAlign::center, VerticalAlign::center);

    //////
    // Duplicit data to wxFontDescriptor
    // used for store/load .3mf file
    //////

    // Height of text line (letters)
    // duplicit to wxFont::PointSize
    float size_in_mm; // [in mm]

    // Additional data about font to be able to find substitution,
    // when same font is not installed
    std::optional<std::string> family;
    std::optional<std::string> face_name;
    std::optional<std::string> style;
    std::optional<std::string> weight;

    /// <summary>
    /// Only constructor with restricted values
    /// </summary>
    /// <param name="line_height">Y size of text [in mm]</param>
    /// <param name="depth">Z size of text [in mm]</param>
    FontProp(float line_height = 10.f) : size_in_mm(line_height), per_glyph(false) {
    }

    bool operator==(const FontProp& other) const {
        auto case0 = is_approx(boldness.value_or(0), other.boldness.value_or(0));
        auto case1 = is_approx(skew.value_or(0), other.skew.value_or(0));
        auto case2 = line_gap.value_or(0) == other.line_gap.value_or(0);
        auto case3 = char_gap.value_or(0) == other.char_gap.value_or(0);
        return            per_glyph == other.per_glyph &&
            align == other.align && is_approx(size_in_mm, other.size_in_mm)
            && case0 && case1 && case2  &&case3;
    }

    // undo / redo stack recovery
    template<class Archive> void save(Archive &ar) const
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::save(ar, char_gap);
        cereal::save(ar, line_gap);
        cereal::save(ar, boldness);
        cereal::save(ar, skew);
        cereal::save(ar, collection_number);
    }
    template<class Archive> void load(Archive &ar)
    {
        ar(size_in_mm, per_glyph, align.first, align.second);
        cereal::load(ar, char_gap);
        cereal::load(ar, line_gap);
        cereal::load(ar, boldness);
        cereal::load(ar, skew);
        cereal::load(ar, collection_number);
    }
};

/// <summary>
/// Style of embossed text
/// (Path + Type) must define how to open font for using on different OS
/// NOTE: OnEdit fix serializations: EmbossStylesSerializable, TextConfigurationSerialization
/// </summary>
struct EmbossStyle
{
    // Human readable name of style it is shown in GUI
    std::string name;

    // Define how to open font
    // Meaning depend on type
    std::string path;

    enum class Type;
    // Define what is stored in path
    Type type { Type::undefined };

    // User modification of font style
    FontProp prop;

    // when name is empty than Font item was loaded from .3mf file
    // and potentionaly it is not reproducable
    // define data stored in path
    // when wx change way of storing add new descriptor Type
    enum class Type {
        undefined = 0,

        // wx font descriptors are platform dependent
        // path is font descriptor generated by wxWidgets
        wx_win_font_descr, // on Windows
        wx_lin_font_descr, // on Linux
        wx_mac_font_descr, // on Max OS

        // TrueTypeFont file loacation on computer
        // for privacy: only filename is stored into .3mf
        file_path
    };

    bool operator==(const EmbossStyle &other) const
    {
        auto case0 = prop == other.prop;
        return type == other.type && case0 && name == other.name;
    }

    // undo / redo stack recovery
    template<class Archive> void serialize(Archive &ar){ ar(name, path, type, prop); }
};

// Emboss style name inside vector is unique
// It is not map beacuse items has own order (view inside of slect)
// It is stored into AppConfig by EmbossStylesSerializable
using EmbossStyles = std::vector<EmbossStyle>;

/// <summary>
/// Define how to create 'Text volume'
/// It is stored into .3mf by TextConfigurationSerialization
/// It is part of ModelVolume optional data
/// </summary>
struct TextConfiguration
{
    // Style of embossed text
    EmbossStyle style;

    // Embossed text value
    std::string text = "None";

    // undo / redo stack recovery
    template<class Archive> void serialize(Archive &ar) { ar(style, text); }
};

} // namespace Slic3r

#endif // slic3r_TextConfiguration_hpp_
