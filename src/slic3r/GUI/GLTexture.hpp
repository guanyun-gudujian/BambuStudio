#ifndef slic3r_GLTexture_hpp_
#define slic3r_GLTexture_hpp_

#include <atomic>
#include <string>
#include <vector>
#include <thread>

#include <wx/colour.h>
#include <wx/font.h>

class wxImage;

namespace Slic3r {
namespace GUI {

    class GLModel;

    class GLTexture
    {
        class Compressor
        {
            struct Level
            {
                unsigned int w;
                unsigned int h;
                std::vector<unsigned char> src_data;
                std::vector<unsigned char> compressed_data;
                bool sent_to_gpu;

                Level(unsigned int w, unsigned int h, const std::vector<unsigned char>& data) : w(w), h(h), src_data(data), sent_to_gpu(false) {}
            };

            GLTexture& m_texture;
            std::vector<Level> m_levels;
            std::thread m_thread;
            // Does the caller want the background thread to stop?
            // This atomic also works as a memory barrier for synchronizing the cancel event with the worker thread.
            std::atomic<bool> m_abort_compressing;
            // How many levels were compressed since the start of the background processing thread?
            // This atomic also works as a memory barrier for synchronizing results of the worker thread with the calling thread.
            std::atomic<unsigned int> m_num_levels_compressed;

        public:
            explicit Compressor(GLTexture& texture) : m_texture(texture), m_abort_compressing(false), m_num_levels_compressed(0) {}
            ~Compressor() { reset(); }

            void reset();

            void add_level(unsigned int w, unsigned int h, const std::vector<unsigned char>& data) { m_levels.emplace_back(w, h, data); }

            void start_compressing();

            bool unsent_compressed_data_available() const;
            void send_compressed_data_to_gpu();
            bool all_compressed_data_sent_to_gpu() const { return m_levels.empty(); }

        private:
            void compress();
        };

    public:
        enum ECompressionType : unsigned char
        {
            None,
            SingleThreaded,
            MultiThreaded
        };
        enum ESamplerWrapMode : uint8_t
        {
            Repeat,
            MirrorRepeat,
            Clamp,
            Border
        };

        struct UV
        {
            float u;
            float v;
        };

        struct Quad_UVs
        {
            UV left_bottom;
            UV right_bottom;
            UV right_top;
            UV left_top;
        };

        static Quad_UVs FullTextureUVs;

    protected:
        unsigned int m_id;
        int m_width;
        int m_height;
        std::string m_source;
        Compressor m_compressor;
        ESamplerWrapMode m_wrap_mode_u{ ESamplerWrapMode::Clamp };
        ESamplerWrapMode m_wrap_mode_v{ ESamplerWrapMode::Clamp };

    public:
        GLTexture();
        virtual ~GLTexture();

        bool load_from_file(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);
        bool load_from_svg_file(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
        //BBS load GLTexture from raw pixel data
        bool load_from_raw_data(std::vector<unsigned char> data, unsigned int w, unsigned int h, bool apply_anisotropy = false);
        // meanings of states: (std::pair<int, bool>)
        // first field (int):
        // 0 -> no changes
        // 1 -> use white only color variant
        // 2 -> use gray only color variant
        // second field (bool):
        // false -> no changes
        // true -> add background color
        bool load_from_svg_files_as_sprites_array(const std::vector<std::string>& filenames, const std::vector<std::pair<int, bool>>& states, unsigned int sprite_size_px, bool compress);
        void reset();
        //BBS: add generate logic for text strings
        int m_original_width;
        int m_original_height;

        bool generate_texture_from_text(const std::string& text_str, wxFont& font, int& ww, int& hh, int &hl, wxColor background = *wxBLACK, wxColor foreground = *wxWHITE);
        bool generate_from_text(const std::string& text_str, wxFont& font, wxColor background = *wxBLACK, wxColor foreground = *wxWHITE);
        bool generate_from_text_string(const std::string& text_str, wxFont& font, wxColor background = *wxBLACK, wxColor foreground = *wxWHITE);

        unsigned int get_id() const { return m_id; }
        int get_original_width() const { return m_original_width; }
        int get_width() const { return m_width; }
        int get_height() const { return m_height; }

        const std::string& get_source() const { return m_source; }

        bool unsent_compressed_data_available() const { return m_compressor.unsent_compressed_data_available(); }
        void send_compressed_data_to_gpu() { m_compressor.send_compressed_data_to_gpu(); }
        bool all_compressed_data_sent_to_gpu() const { return m_compressor.all_compressed_data_sent_to_gpu(); }
        void set_wrap_mode_u(ESamplerWrapMode mode);
        void set_wrap_mode_v(ESamplerWrapMode mode);
        void bind(uint8_t stage = 0);
        void unbind();

        static void render_texture(unsigned int tex_id, float left, float right, float bottom, float top);
        static void render_sub_texture(unsigned int tex_id, float left, float right, float bottom, float top, const Quad_UVs& uvs);
        static void shutdown();
    private:
        bool load_from_png(const std::string& filename, bool use_mipmaps, ECompressionType compression_type, bool apply_anisotropy);
        bool load_from_svg(const std::string& filename, bool use_mipmaps, bool compress, bool apply_anisotropy, unsigned int max_size_px);
        static GLModel& init_model_for_render_image();
        static GLModel& get_model_for_render_image();

        friend class Compressor;
    };

    struct BackgroundTexture
    {
        struct Metadata
        {
            // path of the file containing the background texture
            std::string filename;
            // size of the left edge, in pixels
            unsigned int left;
            // size of the right edge, in pixels
            unsigned int right;
            // size of the top edge, in pixels
            unsigned int top;
            // size of the bottom edge, in pixels
            unsigned int bottom;

            Metadata();
        };

        GLTexture texture;
        Metadata metadata;
    };

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLTexture_hpp_

