/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.hpp"
#include "runtime_internal.hpp"
#include "ini_file.hpp"
#include "input.hpp"
#include <algorithm> // std::all_of, std::find, std::find_if, std::for_each, std::remove_if

extern bool resolve_preset_path(std::filesystem::path &path, std::error_code &ec);

bool reshade::runtime::is_key_down(uint32_t keycode) const
{
	return _input != nullptr && _input->is_key_down(keycode);
}
bool reshade::runtime::is_key_pressed(uint32_t keycode) const
{
	return _input != nullptr && _input->is_key_pressed(keycode);
}
bool reshade::runtime::is_key_released(uint32_t keycode) const
{
	return _input != nullptr && _input->is_key_released(keycode);
}
bool reshade::runtime::is_mouse_button_down(uint32_t button) const
{
	return _input != nullptr && _input->is_mouse_button_down(button);
}
bool reshade::runtime::is_mouse_button_pressed(uint32_t button) const
{
	return _input != nullptr && _input->is_mouse_button_pressed(button);
}
bool reshade::runtime::is_mouse_button_released(uint32_t button) const
{
	return _input != nullptr && _input->is_mouse_button_released(button);
}

uint32_t reshade::runtime::last_key_pressed() const
{
	return _input != nullptr ? _input->last_key_pressed() : 0u;
}
uint32_t reshade::runtime::last_key_released() const
{
	return _input != nullptr ? _input->last_key_released() : 0u;
}

void reshade::runtime::get_mouse_cursor_position(uint32_t *out_x, uint32_t *out_y, int16_t *out_wheel_delta) const
{
	if (out_x != nullptr)
		*out_x = (_input != nullptr) ? _input->mouse_position_x() : 0;
	if (out_y != nullptr)
		*out_y = (_input != nullptr) ? _input->mouse_position_y() : 0;
	if (out_wheel_delta != nullptr)
		*out_wheel_delta = (_input != nullptr) ? _input->mouse_wheel_delta() : 0;
}

void reshade::runtime::block_input_next_frame()
{
#if RESHADE_GUI
	_block_input_next_frame = true;
#endif
}

void reshade::runtime::enumerate_uniform_variables([[maybe_unused]] const char *effect_name, [[maybe_unused]] void(*callback)(effect_runtime *runtime, api::effect_uniform_variable variable, void *user_data), [[maybe_unused]] void *user_data)
{

}

reshade::api::effect_uniform_variable reshade::runtime::find_uniform_variable([[maybe_unused]] const char *effect_name, [[maybe_unused]] const char *variable_name) const
{
	return { 0 };
}

void reshade::runtime::get_uniform_variable_type([[maybe_unused]] api::effect_uniform_variable handle, api::format *out_base_type, uint32_t *out_rows, uint32_t *out_columns, uint32_t *out_array_length) const
{
	if (out_base_type != nullptr)
		*out_base_type = api::format::unknown;
	if (out_rows != nullptr)
		*out_rows = 0;
	if (out_columns != nullptr)
		*out_columns = 0;
	if (out_array_length != nullptr)
		*out_array_length = 0;
}

void reshade::runtime::get_uniform_variable_name([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}
void reshade::runtime::get_uniform_variable_effect_name([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}

bool reshade::runtime::get_annotation_bool_from_uniform_variable([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const char *name, bool *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = false;

	return false;
}
bool reshade::runtime::get_annotation_float_from_uniform_variable([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const char *name, float *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0.0f;

	return false;
}
bool reshade::runtime::get_annotation_int_from_uniform_variable([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const char *name, int32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_uint_from_uniform_variable([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const char *name, uint32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_string_from_uniform_variable([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const char *name, [[maybe_unused]] char *value, size_t *size) const
{
	if (size != nullptr)
		*size = 0;

	return false;
}

void reshade::runtime::reset_uniform_value([[maybe_unused]] api::effect_uniform_variable handle)
{

}

void reshade::runtime::get_uniform_value_bool([[maybe_unused]] api::effect_uniform_variable handle, bool *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = false;
}
void reshade::runtime::get_uniform_value_float([[maybe_unused]] api::effect_uniform_variable handle, float *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0.0f;
}
void reshade::runtime::get_uniform_value_int([[maybe_unused]] api::effect_uniform_variable handle, int32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;
}
void reshade::runtime::get_uniform_value_uint([[maybe_unused]] api::effect_uniform_variable handle, uint32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;
}

void reshade::runtime::set_uniform_value_bool([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const bool *values, [[maybe_unused]] size_t count, [[maybe_unused]] size_t array_index)
{

}
void reshade::runtime::set_uniform_value_float([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const float *values, [[maybe_unused]] size_t count, [[maybe_unused]] size_t array_index)
{

}
void reshade::runtime::set_uniform_value_int([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const int32_t *values, [[maybe_unused]] size_t count, [[maybe_unused]] size_t array_index)
{

}
void reshade::runtime::set_uniform_value_uint([[maybe_unused]] api::effect_uniform_variable handle, [[maybe_unused]] const uint32_t *values, [[maybe_unused]] size_t count, [[maybe_unused]] size_t array_index)
{

}

void reshade::runtime::enumerate_texture_variables([[maybe_unused]] const char *effect_name, [[maybe_unused]] void(*callback)(effect_runtime *runtime, api::effect_texture_variable variable, void *user_data), [[maybe_unused]] void *user_data)
{

}

reshade::api::effect_texture_variable reshade::runtime::find_texture_variable([[maybe_unused]] const char *effect_name, [[maybe_unused]] const char *variable_name) const
{
	return { 0 };
}

void reshade::runtime::get_texture_variable_name([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}
void reshade::runtime::get_texture_variable_effect_name([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}

bool reshade::runtime::get_annotation_bool_from_texture_variable([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const char *name, bool *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = false;

	return false;
}
bool reshade::runtime::get_annotation_float_from_texture_variable([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const char *name, float *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0.0f;

	return false;
}
bool reshade::runtime::get_annotation_int_from_texture_variable([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const char *name, int32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_uint_from_texture_variable([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const char *name, uint32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_string_from_texture_variable([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const char *name, [[maybe_unused]] char *value, size_t *size) const
{
	if (size != nullptr)
		*size = 0;

	return false;
}

void reshade::runtime::update_texture([[maybe_unused]] api::effect_texture_variable handle, [[maybe_unused]] const uint32_t width, [[maybe_unused]] const uint32_t height, [[maybe_unused]] const void *pixels)
{

}

void reshade::runtime::get_texture_binding([[maybe_unused]] api::effect_texture_variable handle, api::resource_view *out_srv, api::resource_view *out_srv_srgb) const
{
	if (out_srv != nullptr)
		*out_srv = { 0 };
	if (out_srv_srgb != nullptr)
		*out_srv_srgb = { 0 };
}

void reshade::runtime::update_texture_bindings([[maybe_unused]] const char *semantic, [[maybe_unused]] api::resource_view srv, [[maybe_unused]] api::resource_view srv_srgb)
{

}

void reshade::runtime::enumerate_techniques([[maybe_unused]] const char *effect_name, [[maybe_unused]] void(*callback)(effect_runtime *runtime, api::effect_technique technique, void *user_data), [[maybe_unused]] void *user_data)
{

}

reshade::api::effect_technique reshade::runtime::find_technique([[maybe_unused]] const char *effect_name, [[maybe_unused]] const char *technique_name)
{
	return { 0 };
}

void reshade::runtime::get_technique_name([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}
void reshade::runtime::get_technique_effect_name([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] char *value, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}

bool reshade::runtime::get_annotation_bool_from_technique([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] const char *name, bool *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = false;

	return false;
}
bool reshade::runtime::get_annotation_float_from_technique([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] const char *name, float *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0.0f;

	return false;
}
bool reshade::runtime::get_annotation_int_from_technique([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] const char *name, int32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_uint_from_technique([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] const char *name, uint32_t *values, size_t count, [[maybe_unused]] size_t array_index) const
{
	for (size_t i = 0; i < count; ++i)
		values[i] = 0;

	return false;
}
bool reshade::runtime::get_annotation_string_from_technique([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] const char *name, [[maybe_unused]] char *value, size_t *size) const
{
	if (size != nullptr)
		*size = 0;

	return false;
}

bool reshade::runtime::get_technique_state([[maybe_unused]] api::effect_technique handle) const
{
	return false;
}
void reshade::runtime::set_technique_state([[maybe_unused]] api::effect_technique handle, [[maybe_unused]] bool enabled)
{

}

constexpr int EFFECT_SCOPE_FLAG = 0b001;
constexpr int PRESET_SCOPE_FLAG = 0b010;
constexpr int GLOBAL_SCOPE_FLAG = 0b100;

void reshade::runtime::set_preprocessor_definition(const char *name, const char *value)
{
	set_preprocessor_definition_for_effect(nullptr, name, value);
}
void reshade::runtime::set_preprocessor_definition_for_effect([[maybe_unused]] const char *effect_name, [[maybe_unused]] const char *name, [[maybe_unused]] const char *value)
{

}
bool reshade::runtime::get_preprocessor_definition(const char *name, char *value, size_t *size) const
{
	return get_preprocessor_definition_for_effect(nullptr, name, value, size);
}
bool reshade::runtime::get_preprocessor_definition_for_effect([[maybe_unused]] const char *effect_name, [[maybe_unused]] const char *name, [[maybe_unused]] char *value, size_t *size) const
{
	if (size != nullptr)
		*size = 0;
	return false;
}

void reshade::runtime::render_effects(api::command_list * /*cmd_list*/, api::resource_view /*rtv*/, api::resource_view /*rtv_srgb*/)
{
}

void reshade::runtime::render_technique(api::effect_technique /*handle*/, api::command_list * /*cmd_list*/, api::resource_view /*rtv*/, api::resource_view /*rtv_srgb*/)
{
}

bool reshade::runtime::get_effects_state() const
{
	return false;
}
void reshade::runtime::set_effects_state([[maybe_unused]] bool enabled)
{

}

void reshade::runtime::get_current_preset_path([[maybe_unused]] char *path, size_t *size) const
{
	if (size == nullptr)
		return;

	*size = 0;
}
void reshade::runtime::set_current_preset_path([[maybe_unused]] const char *path)
{

}

void reshade::runtime::reorder_techniques([[maybe_unused]] size_t count, [[maybe_unused]] const api::effect_technique *techniques)
{

}

#if RESHADE_GUI == 0
bool reshade::runtime::open_overlay(bool /*open*/, api::input_source /*source*/)
{
	return false;
}
#endif

void reshade::runtime::reload_effect_next_frame([[maybe_unused]] const char *effect_name)
{

}
