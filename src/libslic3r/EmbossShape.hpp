#ifndef slic3r_EmbossShape_hpp_
#define slic3r_EmbossShape_hpp_

#include <string>
#include <optional>
#include <memory> // unique_ptr
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/binary.hpp>
#include "Point.hpp" // Transform3d
#include "ExPolygon.hpp"
#include "ExPolygonSerialize.hpp"
#include "nanosvg/nanosvg.h" // NSVGimage

namespace Slic3r {

struct EmbossProjection{
    // Emboss depth, Size in local Z direction
    double depth = 2.f; // [in loacal mm]//Modify By BBS 20241220
    // NOTE: User should see and modify mainly world size not local

    // Flag that result volume use surface cutted from source objects
    bool use_surface = false;
    double embeded_depth = 0.f; // for old depth
    bool   operator==(const EmbossProjection &other) const {
        return depth == other.depth && use_surface == other.use_surface && embeded_depth == other.embeded_depth;
    }

    // undo / redo stack recovery
    template<class Archive> void serialize(Archive &ar) { ar(depth, use_surface, embeded_depth); }
};

// Extend expolygons with information whether it was successfull healed
struct HealedExPolygons{
    ExPolygons expolygons;
    bool is_healed;
    operator ExPolygons&() { return expolygons; }
};
namespace Emboss {
    // description of one letter
    struct Glyph
    {
        // NOTE: shape is scaled by SHAPE_SCALE
        // to be able store points without floating points
        ExPolygons shape;

        // values are in font points
        int advance_width = 0, left_side_bearing = 0;
    };
    // cache for glyph by unicode
    using Glyphs = std::map<int, Glyph>;
    /// <summary>
    /// keep information from file about font
    /// (store file data itself)
    /// + cache data readed from buffer
    /// </summary>
    struct FontFile
    {
        // loaded data from font file
        // must store data size for imgui rasterization
        // To not store data on heap and To prevent unneccesary copy
        // data are stored inside unique_ptr
        std::unique_ptr<std::vector<unsigned char>> data;

        struct Info
        {
            // vertical position is "scale*(ascent - descent + lineGap)"
            int ascent, descent, linegap;

            // for convert font units to pixel
            int unit_per_em;
        };
        // info for each font in data
        std::vector<Info> infos;

        FontFile(std::unique_ptr<std::vector<unsigned char>> data, std::vector<Info> &&infos) : data(std::move(data)), infos(std::move(infos))
        {
            assert(this->data != nullptr);
            assert(!this->data->empty());
        }

        bool operator==(const FontFile &other) const
        {
            if (data->size() != other.data->size()) return false;
            // if(*data != *other.data) return false;
            for (size_t i = 0; i < infos.size(); i++)
                if (infos[i].ascent != other.infos[i].ascent || infos[i].descent == other.infos[i].descent || infos[i].linegap == other.infos[i].linegap) return false;
            return true;
        }
    };

    /// <summary>
    /// Add caching for shape of glyphs
    /// </summary>
    struct FontFileWithCache
    {
        // Pointer on data of the font file
        std::shared_ptr<const FontFile> font_file;

        // Cache for glyph shape
        // IMPORTANT: accessible only in plater job thread !!!
        // main thread only clear cache by set to another shared_ptr
        std::shared_ptr<Emboss::Glyphs> cache;

        FontFileWithCache() : font_file(nullptr), cache(nullptr) {}
        explicit FontFileWithCache(std::unique_ptr<FontFile> font_file) : font_file(std::move(font_file)), cache(std::make_shared<Emboss::Glyphs>()) {}
        bool has_value() const { return font_file != nullptr && cache != nullptr; }
    };
}
// Help structure to identify expolygons grups
// e.g. emboss -> per glyph -> identify character
struct ExPolygonsWithId
{
    // Identificator for shape
    // In text it separate letters and the name is unicode value of letter
    // Is svg it is id of path
    unsigned id;

    // shape defined by integer point contain only lines
    // Curves are converted to sequence of lines
    ExPolygons expoly;

    // flag whether expolygons are fully healed(without duplication)
    bool is_healed = true;
};
using ExPolygonsWithIds = std::vector<ExPolygonsWithId>;

/// <summary>
/// Contain plane shape information to be able emboss it and edit it
/// </summary>
struct EmbossShape
{
    // shapes to to emboss separately over surface
    ExPolygonsWithIds shapes_with_ids;
    // Only cache for final shape
    // It is calculated from ExPolygonsWithIds
    // Flag is_healed --> whether union of shapes is healed
    // Healed mean without selfintersection and point duplication
    HealedExPolygons final_shape;

    // scale of shape, multiplier to get 3d point in mm from integer shape
    double scale = SCALING_FACTOR;

    // Define how to emboss shape
    EmbossProjection projection;

    // !!! Volume stored in .3mf has transformed vertices.
    // (baked transformation into vertices position)
    // Only place for fill this is when load from .3mf
    // This is correction for volume transformation
    // Stored_Transform3d * fix_3mf_tr = Transform3d_before_store_to_3mf
    std::optional<Slic3r::Transform3d> fix_3mf_tr;

    struct SvgFile {
        // File(.svg) path on local computer
        // When empty can't reload from disk
        std::string path;

        // File path into .3mf(.zip)
        // When empty svg is not stored into .3mf file yet.
        // and will create dialog to delete private data on save.
        std::string path_in_3mf;

        // Loaded svg file data.
        // !!! It is not serialized on undo/redo stack
        std::shared_ptr<NSVGimage> image = nullptr;

        // Loaded string data from file
        std::shared_ptr<std::string> file_data = nullptr;

        template<class Archive> void save(Archive &ar) const {
            // Note: image is only cache it is not neccessary to store

            // Store file data as plain string
            // For Embossed text file_data are nullptr
            ar(path, path_in_3mf, (file_data != nullptr) ? *file_data : std::string(""));
        }
        template<class Archive> void load(Archive &ar) {
            // for restore shared pointer on file data
            std::string file_data_str;
            ar(path, path_in_3mf, file_data_str);
            if (!file_data_str.empty())
                file_data = std::make_unique<std::string>(file_data_str);
        }
    };
    // When embossing shape is made by svg file this is source data
    std::optional<SvgFile> svg_file;
    std::vector<float> text_scales;
    // undo / redo stack recovery
    template<class Archive> void save(Archive &ar) const
    {
        // final_shape is not neccessary to store - it is only cache
        ar(shapes_with_ids, final_shape, scale, projection, svg_file);
        cereal::save(ar, fix_3mf_tr);
    }
    template<class Archive> void load(Archive &ar)
    {
        ar(shapes_with_ids, final_shape, scale, projection, svg_file);
        cereal::load(ar, fix_3mf_tr);
    }
};
} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
template<class Archive> void serialize(Archive &ar, Slic3r::ExPolygonsWithId &o) { ar(o.id, o.expoly, o.is_healed); }
template<class Archive> void serialize(Archive &ar, Slic3r::HealedExPolygons &o) { ar(o.expolygons, o.is_healed); }
}; // namespace cereal

#endif // slic3r_EmbossShape_hpp_
