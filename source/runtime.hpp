/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "reshade_api.hpp"
#include "state_block.hpp"
#include <chrono>
#include <memory>
#include <filesystem>
#include <atomic>
#include <shared_mutex>

struct ImFont;
struct ImDrawData;
struct ImGuiContext;

class ini_file;
namespace reshadefx { struct sampler_desc; }

namespace reshade
{
	struct effect;
	struct uniform;
	struct texture;
	struct technique;

	/// <summary>
	/// The main ReShade post-processing effect runtime.
	/// </summary>
	class __declspec(uuid("77FF8202-5BEC-42AD-8CE0-397F3E84EAA6")) runtime : public api::effect_runtime
	{
	public:
		runtime(api::swapchain *swapchain, api::command_queue *graphics_queue, const std::filesystem::path &config_path, bool is_vr);
		~runtime();

		bool on_init();
		void on_reset();
		void on_present(api::command_queue *present_queue);

		uint64_t get_native() const final { return _swapchain->get_native(); }

		void get_private_data(const uint8_t guid[16], uint64_t *data) const final { return _swapchain->get_private_data(guid, data); }
		void set_private_data(const uint8_t guid[16], const uint64_t data)  final { return _swapchain->set_private_data(guid, data); }

		api::device *get_device() final { return _device; }
		api::swapchain *get_swapchain() { return _swapchain; }
		api::command_queue *get_command_queue() final { return _graphics_queue; }

		void *get_hwnd() const final { return _swapchain->get_hwnd(); }

		api::resource get_back_buffer(uint32_t index) final { return _swapchain->get_back_buffer(index); }
		uint32_t get_back_buffer_count() const final { return _swapchain->get_back_buffer_count(); }
		uint32_t get_current_back_buffer_index() const final { return _swapchain->get_current_back_buffer_index(); }

		/// <summary>
		/// Gets the path to the configuration file used by this effect runtime.
		/// </summary>
		const std::filesystem::path &get_config_path() const { return _config_path; }

		void render_effects(api::command_list *cmd_list, api::resource_view rtv, api::resource_view rtv_srgb) final;
		void render_technique(api::effect_technique handle, api::command_list *cmd_list, api::resource_view rtv, api::resource_view rtv_srgb) final;

		/// <summary>
		/// Captures a screenshot of the current back buffer resource and writes it to an image file on disk.
		/// </summary>
		void save_screenshot(const std::string_view postfix = std::string_view());
		bool capture_screenshot(void *pixels) final { return get_texture_data(_back_buffer_resolved != 0 ? _back_buffer_resolved : _swapchain->get_current_back_buffer(), _back_buffer_resolved != 0 ? api::resource_usage::render_target : api::resource_usage::present, static_cast<uint8_t *>(pixels)); }

		void get_screenshot_width_and_height(uint32_t *out_width, uint32_t *out_height) const final { *out_width = _width; *out_height = _height; }

		bool is_key_down(uint32_t keycode) const final;
		bool is_key_pressed(uint32_t keycode) const final;
		bool is_key_released(uint32_t keycode) const final;
		bool is_mouse_button_down(uint32_t button) const final;
		bool is_mouse_button_pressed(uint32_t button) const final;
		bool is_mouse_button_released(uint32_t button) const final;

		uint32_t last_key_pressed() const final;
		uint32_t last_key_released() const final;

		void get_mouse_cursor_position(uint32_t *out_x, uint32_t *out_y, int16_t *out_wheel_delta) const final;

		void block_input_next_frame() final;

		void enumerate_uniform_variables(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_uniform_variable variable, void *user_data), void *user_data) final;

		api::effect_uniform_variable find_uniform_variable(const char *effect_name, const char *variable_name) const final;

		void get_uniform_variable_type(api::effect_uniform_variable variable, api::format *out_base_type, uint32_t *out_rows, uint32_t *out_columns, uint32_t *out_array_length) const final;

		void get_uniform_variable_name(api::effect_uniform_variable variable, char *name, size_t *name_size) const final;
		void get_uniform_variable_effect_name(api::effect_uniform_variable variable, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_uniform_variable(api::effect_uniform_variable variable, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_uniform_variable(api::effect_uniform_variable variable, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_uniform_variable(api::effect_uniform_variable variable, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_uniform_variable(api::effect_uniform_variable variable, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_uniform_variable(api::effect_uniform_variable variable, const char *name, char *value, size_t *value_size) const final;

		void reset_uniform_value(api::effect_uniform_variable variable);

		void get_uniform_value_bool(api::effect_uniform_variable variable, bool *values, size_t count, size_t array_index) const final;
		void get_uniform_value_float(api::effect_uniform_variable variable, float *values, size_t count, size_t array_index) const final;
		void get_uniform_value_int(api::effect_uniform_variable variable, int32_t *values, size_t count, size_t array_index) const final;
		void get_uniform_value_uint(api::effect_uniform_variable variable, uint32_t *values, size_t count, size_t array_index) const final;

		void set_uniform_value_bool(api::effect_uniform_variable variable, const bool *values, size_t count, size_t array_index) final;
		void set_uniform_value_float(api::effect_uniform_variable variable, const float *values, size_t count, size_t array_index) final;
		void set_uniform_value_int(api::effect_uniform_variable variable, const int32_t *values, size_t count, size_t array_index) final;
		void set_uniform_value_uint(api::effect_uniform_variable variable, const uint32_t *values, size_t count, size_t array_index) final;

		void enumerate_texture_variables(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_texture_variable variable, void *user_data), void *user_data) final;

		api::effect_texture_variable find_texture_variable(const char *effect_name, const char *variable_name) const final;

		void get_texture_variable_name(api::effect_texture_variable variable, char *name, size_t *name_size) const final;
		void get_texture_variable_effect_name(api::effect_texture_variable variable, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_texture_variable(api::effect_texture_variable variable, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_texture_variable(api::effect_texture_variable variable, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_texture_variable(api::effect_texture_variable variable, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_texture_variable(api::effect_texture_variable variable, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_texture_variable(api::effect_texture_variable variable, const char *name, char *value, size_t *value_size) const final;

		void update_texture(api::effect_texture_variable variable, const uint32_t width, const uint32_t height, const void *pixels) final;

		void get_texture_binding(api::effect_texture_variable variable, api::resource_view *out_srv, api::resource_view *out_srv_srgb) const final;

		void update_texture_bindings(const char *semantic, api::resource_view srv, api::resource_view srv_srgb) final;

		void enumerate_techniques(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_technique technique, void *user_data), void *user_data) final;

		api::effect_technique find_technique(const char *effect_name, const char *technique_name) final;

		void get_technique_name(api::effect_technique technique, char *name, size_t *name_size) const final;
		void get_technique_effect_name(api::effect_technique technique, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_technique(api::effect_technique technique, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_technique(api::effect_technique technique, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_technique(api::effect_technique technique, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_technique(api::effect_technique technique, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_technique(api::effect_technique technique, const char *name, char *value, size_t *value_size) const final;

		bool get_technique_state(api::effect_technique technique) const final;
		void set_technique_state(api::effect_technique technique, bool enabled) final;

		bool get_preprocessor_definition(const char *name, char *value, size_t *value_size) const final;
		bool get_preprocessor_definition_for_effect(const char *effect_name, const char *name, char *value, size_t *value_size) const final;
		void set_preprocessor_definition(const char *name, const char *value) final;
		void set_preprocessor_definition_for_effect(const char *effect_name, const char *name, const char *value) final;

		bool get_effects_state() const final;
		void set_effects_state(bool enabled) final;

		void get_current_preset_path(char *path, size_t *path_size) const final;
		void set_current_preset_path(const char *path) final;

		void reorder_techniques(size_t count, const api::effect_technique *techniques) final;

		bool open_overlay(bool open, api::input_source source) final;

		void set_color_space(api::color_space color_space) final { _back_buffer_color_space = color_space; }

		void reload_effect_next_frame(const char *effect_name) final;

	private:
		void load_config();
		void save_config() const;

		void save_current_preset() const final {}

		bool get_texture_data(api::resource resource, api::resource_usage state, uint8_t *pixels);

		bool execute_screenshot_post_save_command(const std::filesystem::path &screenshot_path, unsigned int screenshot_count);

		api::swapchain *const _swapchain;
		api::device *const _device;
		api::command_queue *const _graphics_queue;
		unsigned int _width = 0;
		unsigned int _height = 0;
		unsigned int _vendor_id = 0;
		unsigned int _device_id = 0;
		unsigned int _renderer_id = 0;
		uint16_t _back_buffer_samples = 1;
		api::format _back_buffer_format = api::format::unknown;
		api::color_space _back_buffer_color_space = api::color_space::unknown;

		#pragma region Status
		bool _is_initialized = false;
		bool _preset_save_successful = true;
		std::filesystem::path _config_path;

		bool _ignore_shortcuts = false;
		bool _force_shortcut_modifiers = true;
		std::shared_ptr<class input> _input;
		std::shared_ptr<class input_gamepad> _input_gamepad;

		std::chrono::high_resolution_clock::duration _last_frame_duration;
		std::chrono::high_resolution_clock::time_point _start_time, _last_present_time;
		uint64_t _frame_count = 0;
		#pragma endregion

		#pragma region Effect Loading
		std::vector<std::thread> _worker_threads;
		std::chrono::high_resolution_clock::time_point _last_reload_time;
		#pragma endregion

		#pragma region Effect Rendering
		api::pipeline _copy_pipeline = {};
		api::pipeline_layout _copy_pipeline_layout = {};
		api::sampler  _copy_sampler_state = {};

		api::resource _back_buffer_resolved = {};
		api::resource_view _back_buffer_resolved_srv = {};
		std::vector<api::resource_view> _back_buffer_targets;

		api::state_block _app_state = {};

		api::fence _queue_sync_fence = {};
		uint64_t _queue_sync_value = 0;
		#pragma endregion

		#pragma region Screenshot
#if RESHADE_GUI
		bool _screenshot_save_gui = false;
#endif
		bool _screenshot_clear_alpha = true;
		unsigned int _screenshot_count = 0;
		unsigned int _screenshot_format = 1;
		unsigned int _screenshot_jpeg_quality = 90;
		unsigned int _screenshot_key_data[4] = {};
		std::filesystem::path _screenshot_sound_path;
		std::filesystem::path _screenshot_path;
		std::string _screenshot_name;
		std::filesystem::path _screenshot_post_save_command;
		std::string _screenshot_post_save_command_arguments;
		std::filesystem::path _screenshot_post_save_command_working_directory;
		bool _screenshot_post_save_command_hide_window = false;

		bool _should_save_screenshot = false;
		std::atomic<bool> _last_screenshot_save_successful = true;
		bool _screenshot_directory_creation_successful = true;
		std::filesystem::path _last_screenshot_file;
		std::chrono::high_resolution_clock::time_point _last_screenshot_time;
		#pragma endregion

#if RESHADE_GUI
		void init_gui();
		bool init_gui_vr();
		void deinit_gui();
		void deinit_gui_vr();
		void build_font_atlas();

		void load_config_gui(const ini_file &config);
		void save_config_gui(ini_file &config) const;

		void load_custom_style();
		void save_custom_style() const;

		void draw_gui();

		void draw_gui_about();
		bool init_imgui_resources();
		void render_imgui_draw_data(api::command_list *cmd_list, ImDrawData *draw_data, api::resource_view rtv);
		void destroy_imgui_resources();

		#pragma region Overlay
		ImGuiContext *_imgui_context = nullptr;

		bool _show_splash = true;
		bool _show_overlay = false;
		unsigned int _show_fps = 2;
		unsigned int _show_clock = false;
		unsigned int _show_frametime = false;
		unsigned int _show_preset_name = false;
		bool _show_screenshot_message = true;

		bool _is_font_scaling = false;
		bool _no_font_scaling = false;
		bool _block_input_next_frame = false;
		unsigned int _overlay_key_data[4];
		unsigned int _fps_key_data[4] = {};
		unsigned int _frametime_key_data[4] = {};
		unsigned int _fps_pos = 1;
		unsigned int _clock_format = 0;
		unsigned int _input_processing_mode = 2;

		api::resource _font_atlas_tex = {};
		api::resource_view _font_atlas_srv = {};

		api::pipeline _imgui_pipeline = {};
		api::pipeline_layout _imgui_pipeline_layout = {};
		api::sampler  _imgui_sampler_state = {};

		int _imgui_num_indices[4] = {};
		api::resource _imgui_indices[4] = {};
		int _imgui_num_vertices[4] = {};
		api::resource _imgui_vertices[4] = {};

		api::resource _vr_overlay_tex = {};
		api::resource_view _vr_overlay_target = {};
		#pragma endregion

		#pragma region Overlay Home
		#pragma endregion

		#pragma region Overlay Add-ons
		char _addons_filter[32] = {};
		#pragma endregion

		#pragma region Overlay Settings
		std::string _selected_language, _current_language;
		int _font_size = 0;
		int _editor_font_size = 0;
		int _style_index = 2;
		int _editor_style_index = 0;
		std::filesystem::path _font_path, _default_font_path;
		std::filesystem::path _latin_font_path;
		std::filesystem::path _editor_font_path, _default_editor_font_path;
		std::filesystem::path _file_selection_path;
		float _fps_col[4] = { 1.0f, 1.0f, 0.784314f, 1.0f };
		float _fps_scale = 1.0f;
		float _hdr_overlay_brightness = 203.f; // HDR reference white as per BT.2408
		api::color_space _hdr_overlay_overwrite_color_space = api::color_space::unknown;
		#pragma endregion

		#pragma region Overlay Statistics
		#pragma endregion

		#pragma region Overlay Log
		char _log_filter[32] = {};
		bool _log_wordwrap = false;
		uintmax_t _last_log_size;
		std::vector<std::string> _log_lines;
		#pragma endregion
#endif
	};
}
