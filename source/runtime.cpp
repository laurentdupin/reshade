/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.hpp"
#include "runtime_internal.hpp"
#include "version.h"
#include "dll_log.hpp"
#include "dll_resources.hpp"
#include "input.hpp"
#include "com_ptr.hpp"
#include "platform_utils.hpp"
#include "reshade_api_object_impl.hpp"
#include <set>
#include <thread>
#include <cmath> // std::abs, std::fmod
#include <cctype> // std::toupper
#include <cwctype> // std::towlower
#include <cstdio> // std::snprintf
#include <cstdlib> // std::malloc, std::rand, std::strtod, std::strtol
#include <cstring> // std::memcpy, std::memset, std::strlen
#include <charconv> // std::to_chars
#include <algorithm> // std::all_of, std::copy_n, std::equal, std::fill_n, std::find, std::find_if, std::for_each, std::max, std::min, std::replace, std::remove, std::remove_if, std::reverse, std::search, std::sort, std::stable_sort, std::swap, std::transform
#include <fpng.h>
#include <stb_image.h>
#include <stb_image_dds.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>
#include <d3dcompiler.h>

bool resolve_path(std::filesystem::path &path, std::error_code &ec)
{
	// Finally try to canonicalize the path too
	if (std::filesystem::path canonical_path = std::filesystem::canonical(path, ec); !ec)
		path = std::move(canonical_path);
	else
		path = path.lexically_normal();
	return !ec; // The canonicalization step fails if the path does not exist
}

reshade::runtime::runtime(api::swapchain *swapchain, api::command_queue *graphics_queue) :
	_swapchain(swapchain),
	_device(swapchain->get_device()),
	_graphics_queue(graphics_queue),
	_start_time(std::chrono::high_resolution_clock::now()),
	_last_present_time(_start_time),
	_last_frame_duration(std::chrono::milliseconds(1))
{
	assert(swapchain != nullptr && graphics_queue != nullptr);

	_device->get_property(api::device_properties::vendor_id, &_vendor_id);
	_device->get_property(api::device_properties::device_id, &_device_id);

	_device->get_property(api::device_properties::api_version, &_renderer_id);
	switch (_device->get_api())
	{
	case api::device_api::d3d9:
	case api::device_api::d3d10:
	case api::device_api::d3d11:
	case api::device_api::d3d12:
		break;
	case api::device_api::opengl:
		_renderer_id |= 0x10000;
		break;
	case api::device_api::vulkan:
		_renderer_id |= 0x20000;
		break;
	}

	char device_description[256] = "";
	_device->get_property(api::device_properties::description, device_description);

	if (uint32_t driver_version = 0;
		_device->get_property(api::device_properties::driver_version, &driver_version))
		log::message(log::level::info, "Running on %s Driver %u.%u.", device_description, driver_version / 100, driver_version % 100);
	else
		log::message(log::level::info, "Running on %s.", device_description);

#if RESHADE_GUI
	init_gui();
#endif

	fpng::fpng_init();
}
reshade::runtime::~runtime()
{
	assert(_worker_threads.empty());

#if RESHADE_GUI
	deinit_gui();
#endif
}

bool reshade::runtime::on_init()
{
	assert(!_is_initialized);

	const api::resource_desc back_buffer_desc = _device->get_resource_desc(get_back_buffer(0));

	// Avoid initializing on very small swap chains (e.g. implicit swap chain in The Sims 4, which is not used to present in windowed mode)
	if (back_buffer_desc.texture.width <= 16 && back_buffer_desc.texture.height <= 16)
		return false;

	_width = back_buffer_desc.texture.width;
	_height = back_buffer_desc.texture.height;
	_back_buffer_format = api::format_to_default_typed(back_buffer_desc.texture.format);
	_back_buffer_samples = back_buffer_desc.texture.samples;
	_back_buffer_color_space = _swapchain->get_color_space();

	// Create resolve texture and copy pipeline (do this before creating effect resources, to ensure correct back buffer format is set up)
	if (back_buffer_desc.texture.samples > 1
		// Always use resolve texture in OpenGL to flip vertically and support sRGB + binding effect stencil
		|| (_device->get_api() == api::device_api::opengl)
		)
	{
		const bool need_copy_pipeline =
			_device->get_api() == api::device_api::d3d10 ||
			_device->get_api() == api::device_api::d3d11 ||
			_device->get_api() == api::device_api::d3d12;

		api::resource_usage usage = api::resource_usage::render_target | api::resource_usage::copy_dest | api::resource_usage::resolve_dest;
		if (need_copy_pipeline)
			usage |= api::resource_usage::shader_resource;
		else
			usage |= api::resource_usage::copy_source;

		if (!_device->create_resource(
				api::resource_desc(_width, _height, 1, 1, api::format_to_typeless(_back_buffer_format), 1, api::memory_heap::gpu_only, usage),
				nullptr, back_buffer_desc.texture.samples == 1 ? api::resource_usage::copy_dest : api::resource_usage::resolve_dest, &_back_buffer_resolved) ||
			!_device->create_resource_view(
				_back_buffer_resolved,
				api::resource_usage::render_target,
				api::resource_view_desc(api::format_to_default_typed(_back_buffer_format, 0)),
				&_back_buffer_targets.emplace_back()) ||
			!_device->create_resource_view(
				_back_buffer_resolved,
				api::resource_usage::render_target,
				api::resource_view_desc(api::format_to_default_typed(_back_buffer_format, 1)),
				&_back_buffer_targets.emplace_back()))
		{
			log::message(log::level::error, "Failed to create resolve texture resource!");
			goto exit_failure;
		}

		if (need_copy_pipeline)
		{
			if (!_device->create_resource_view(
					_back_buffer_resolved,
					api::resource_usage::shader_resource,
					api::resource_view_desc(_back_buffer_format),
					&_back_buffer_resolved_srv))
			{
				log::message(log::level::error, "Failed to create resolve shader resource view!");
				goto exit_failure;
			}

			api::sampler_desc sampler_desc = {};
			sampler_desc.filter = api::filter_mode::min_mag_mip_point;
			sampler_desc.address_u = api::texture_address_mode::clamp;
			sampler_desc.address_v = api::texture_address_mode::clamp;
			sampler_desc.address_w = api::texture_address_mode::clamp;

			api::pipeline_layout_param layout_params[2];
			layout_params[0] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::all, 1, api::descriptor_type::sampler };
			layout_params[1] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::all, 1, api::descriptor_type::shader_resource_view };

			const resources::data_resource vs = resources::load_data_resource(IDR_FULLSCREEN_VS);
			const resources::data_resource ps = resources::load_data_resource(IDR_COPY_PS);

			api::shader_desc vs_desc = { vs.data, vs.data_size };
			api::shader_desc ps_desc = { ps.data, ps.data_size };

			std::vector<api::pipeline_subobject> subobjects;
			subobjects.push_back({ api::pipeline_subobject_type::vertex_shader, 1, &vs_desc });
			subobjects.push_back({ api::pipeline_subobject_type::pixel_shader, 1, &ps_desc });

			if (!_device->create_pipeline_layout(2, layout_params, &_copy_pipeline_layout) ||
				!_device->create_pipeline(_copy_pipeline_layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &_copy_pipeline) ||
				!_device->create_sampler(sampler_desc, &_copy_sampler_state))
			{
				log::message(log::level::error, "Failed to create copy pipeline!");
				goto exit_failure;
			}
		}
	}

	// Create render targets for the back buffer resources
	for (uint32_t i = 0, count = get_back_buffer_count(); i < count; ++i)
	{
		const api::resource back_buffer_resource = get_back_buffer(i);

		if (!_device->create_resource_view(
				back_buffer_resource,
				api::resource_usage::render_target,
				api::resource_view_desc(
					back_buffer_desc.texture.samples > 1 ? api::resource_view_type::texture_2d_multisample : api::resource_view_type::texture_2d,
					api::format_to_default_typed(back_buffer_desc.texture.format, 0), 0, 1, 0, 1),
				&_back_buffer_targets.emplace_back()) ||
			!_device->create_resource_view(
				back_buffer_resource,
				api::resource_usage::render_target,
				api::resource_view_desc(
					back_buffer_desc.texture.samples > 1 ? api::resource_view_type::texture_2d_multisample : api::resource_view_type::texture_2d,
					api::format_to_default_typed(back_buffer_desc.texture.format, 1), 0, 1, 0, 1),
				&_back_buffer_targets.emplace_back()))
		{
			log::message(log::level::error, "Failed to create back buffer render targets!");
			goto exit_failure;
		}
	}

	create_state_block(_device, &_app_state);

#if RESHADE_GUI
	if (!init_imgui_resources())
		goto exit_failure;
#endif

	const input::window_handle window = _swapchain->get_hwnd();
	if (window != nullptr)
		_input = input::register_window(window);
	else
		_input.reset();

	// GTK 3 enables transparency for windows, which messes with effects that do not return an alpha value, so disable that again
	if (window != nullptr)
		utils::set_window_transparency(window, false);

	// Reset frame count to zero so effects are loaded in 'update_effects'
	_frame_count = 0;

	_is_initialized = true;
	_last_reload_time = std::chrono::high_resolution_clock::now(); // Intentionally set to current time, so that duration to last reload is valid even when there is no reload on init

	_preset_save_successful = true;

	return true;

exit_failure:

	_device->destroy_pipeline(_copy_pipeline);
	_copy_pipeline = {};
	_device->destroy_pipeline_layout(_copy_pipeline_layout);
	_copy_pipeline_layout = {};
	_device->destroy_sampler(_copy_sampler_state);
	_copy_sampler_state = {};

	_device->destroy_resource(_back_buffer_resolved);
	_back_buffer_resolved = {};
	_device->destroy_resource_view(_back_buffer_resolved_srv);
	_back_buffer_resolved_srv = {};

	for (const api::resource_view view : _back_buffer_targets)
		_device->destroy_resource_view(view);
	_back_buffer_targets.clear();

	destroy_state_block(_device, _app_state);
	_app_state = {};

#if RESHADE_GUI
	destroy_imgui_resources();
#endif

	return false;
}
void reshade::runtime::on_reset()
{
	if (_is_initialized)
		// Update initialization state immediately, so that any effect loading still in progress can abort early
		_is_initialized = false;
	else
		return; // Nothing to do if the runtime was already destroyed or not successfully initialized in the first place

	for (std::thread &thread : _worker_threads)
		if (thread.joinable())
			thread.join();
	_worker_threads.clear();

	_device->destroy_pipeline(_copy_pipeline);
	_copy_pipeline = {};
	_device->destroy_pipeline_layout(_copy_pipeline_layout);
	_copy_pipeline_layout = {};
	_device->destroy_sampler(_copy_sampler_state);
	_copy_sampler_state = {};

	_device->destroy_resource(_back_buffer_resolved);
	_back_buffer_resolved = {};
	_device->destroy_resource_view(_back_buffer_resolved_srv);
	_back_buffer_resolved_srv = {};

	for (const api::resource_view view : _back_buffer_targets)
		_device->destroy_resource_view(view);
	_back_buffer_targets.clear();

	destroy_state_block(_device, _app_state);
	_app_state = {};

	_device->destroy_fence(_queue_sync_fence);
	_queue_sync_fence = {};

	_width = _height = 0;

#if RESHADE_GUI
	destroy_imgui_resources();
#endif
}
void reshade::runtime::on_present(api::command_queue *present_queue)
{
	assert(present_queue != nullptr);

	if (!_is_initialized)
		return;

	// If the application is presenting with a different queue than rendering, synchronize these two queues first
	// This ensures that it has finished rendering before ReShade applies its own rendering
	if (present_queue != _graphics_queue)
	{
		if (_queue_sync_fence == 0)
		{
			if (!_device->create_fence(_queue_sync_value, api::fence_flags::none, &_queue_sync_fence))
				log::message(log::level::error, "Failed to create queue synchronization fence!");
		}

		if (_queue_sync_fence != 0)
		{
			_queue_sync_value++;

			// Signal from the queue the application is presenting with
			if (present_queue->signal(_queue_sync_fence, _queue_sync_value))
				// Wait on that before the immediate command list flush below
				_graphics_queue->wait(_queue_sync_fence, _queue_sync_value);
		}
	}

	api::command_list *const cmd_list = _graphics_queue->get_immediate_command_list();

	capture_state(cmd_list, _app_state);

	uint32_t back_buffer_index = (_back_buffer_resolved != 0 ? 2 : 0) + get_current_back_buffer_index() * 2;
	const api::resource back_buffer_resource = _device->get_resource_from_view(_back_buffer_targets[back_buffer_index]);

	// Resolve MSAA back buffer if MSAA is active or copy when format conversion is required
	if (_back_buffer_resolved != 0)
	{
		if (_back_buffer_samples == 1)
		{
			cmd_list->barrier(back_buffer_resource, api::resource_usage::present, api::resource_usage::copy_source);
			cmd_list->copy_texture_region(back_buffer_resource, 0, nullptr, _back_buffer_resolved, 0, nullptr);
			cmd_list->barrier(_back_buffer_resolved, api::resource_usage::copy_dest, api::resource_usage::render_target);
		}
		else
		{
			cmd_list->barrier(back_buffer_resource, api::resource_usage::present, api::resource_usage::resolve_source);
			cmd_list->resolve_texture_region(back_buffer_resource, 0, nullptr, _back_buffer_resolved, 0, 0, 0, 0, _back_buffer_format);
			cmd_list->barrier(_back_buffer_resolved, api::resource_usage::resolve_dest, api::resource_usage::render_target);
		}
	}

	// Lock input so it cannot be modified by other threads while we are reading it here
	std::unique_lock<std::recursive_mutex> input_lock;
	if (_input != nullptr)
		input_lock = _input->lock();

	_frame_count++;
	const auto current_time = std::chrono::high_resolution_clock::now();
	_last_frame_duration = current_time - _last_present_time; _last_present_time = current_time;

#if RESHADE_GUI
	// Draw overlay
	draw_gui();
#endif

	// Stretch main render target back into MSAA back buffer if MSAA is active or copy when format conversion is required
	if (_back_buffer_resolved != 0)
	{
		const api::resource resources[2] = { back_buffer_resource, _back_buffer_resolved };
		const api::resource_usage state_old[2] = { api::resource_usage::copy_source | api::resource_usage::resolve_source, api::resource_usage::render_target };
		const api::resource_usage state_final[2] = { api::resource_usage::present, api::resource_usage::resolve_dest };

		if (_device->get_api() == api::device_api::d3d10 ||
			_device->get_api() == api::device_api::d3d11 ||
			_device->get_api() == api::device_api::d3d12)
		{
			const api::resource_usage state_new[2] = { api::resource_usage::render_target, api::resource_usage::shader_resource };

			cmd_list->barrier(2, resources, state_old, state_new);

			cmd_list->bind_pipeline(api::pipeline_stage::all_graphics, _copy_pipeline);

			cmd_list->push_descriptors(api::shader_stage::pixel, _copy_pipeline_layout, 0, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::sampler, &_copy_sampler_state });
			cmd_list->push_descriptors(api::shader_stage::pixel, _copy_pipeline_layout, 1, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::shader_resource_view, &_back_buffer_resolved_srv });

			const api::viewport viewport = { 0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f };
			cmd_list->bind_viewports(0, 1, &viewport);
			const api::rect scissor_rect = { 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height) };
			cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

			const bool srgb_write_enable = (_back_buffer_format == api::format::r8g8b8a8_unorm_srgb || _back_buffer_format == api::format::b8g8r8a8_unorm_srgb);
			cmd_list->bind_render_targets_and_depth_stencil(1, &_back_buffer_targets[back_buffer_index + srgb_write_enable]);

			cmd_list->draw(3, 1, 0, 0);

			cmd_list->barrier(2, resources, state_new, state_final);
		}
		else
		{
			const api::resource_usage state_new[2] = { api::resource_usage::copy_dest, api::resource_usage::copy_source };

			cmd_list->barrier(2, resources, state_old, state_new);
			cmd_list->copy_texture_region(_back_buffer_resolved, 0, nullptr, back_buffer_resource, 0, nullptr);
			cmd_list->barrier(2, resources, state_new, state_final);
		}
	}

	// Apply previous state from application
	apply_state(cmd_list, _app_state);

	if (present_queue != _graphics_queue && _queue_sync_fence != 0)
	{
		_queue_sync_value++;

		if (_graphics_queue->signal(_queue_sync_fence, _queue_sync_value))
			present_queue->wait(_queue_sync_fence, _queue_sync_value);
	}

	// Update input status
	if (_input != nullptr)
		_input->next_frame();
}

static std::string expand_macro_string(const std::string &input, std::vector<std::pair<std::string, std::string>> macros)
{
	const auto now = std::chrono::system_clock::now();
	const auto now_seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	char timestamp[21];
	const std::time_t t = std::chrono::system_clock::to_time_t(now_seconds);
	struct tm tm; localtime_s(&tm, &t);

	std::snprintf(timestamp, std::size(timestamp), "%.4d-%.2d-%.2d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	macros.emplace_back("Date", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.4d", tm.tm_year + 1900);
	macros.emplace_back("DateYear", timestamp);
	macros.emplace_back("Year", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.2d", tm.tm_mon + 1);
	macros.emplace_back("DateMonth", timestamp);
	macros.emplace_back("Month", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.2d", tm.tm_mday);
	macros.emplace_back("DateDay", timestamp);
	macros.emplace_back("Day", timestamp);

	std::snprintf(timestamp, std::size(timestamp), "%.2d-%.2d-%.2d", tm.tm_hour, tm.tm_min, tm.tm_sec);
	macros.emplace_back("Time", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.2d", tm.tm_hour);
	macros.emplace_back("TimeHour", timestamp);
	macros.emplace_back("Hour", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.2d", tm.tm_min);
	macros.emplace_back("TimeMinute", timestamp);
	macros.emplace_back("Minute", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.2d", tm.tm_sec);
	macros.emplace_back("TimeSecond", timestamp);
	macros.emplace_back("Second", timestamp);
	std::snprintf(timestamp, std::size(timestamp), "%.3lld", std::chrono::duration_cast<std::chrono::milliseconds>(now - now_seconds).count());
	macros.emplace_back("TimeMillisecond", timestamp);
	macros.emplace_back("Millisecond", timestamp);
	macros.emplace_back("TimeMS", timestamp);

	std::string result;

	for (size_t offset = 0, macro_beg, macro_end; offset < input.size(); offset = macro_end + 1)
	{
		macro_beg = input.find('%', offset);
		macro_end = input.find('%', macro_beg + 1);

		if (macro_beg == std::string::npos || macro_end == std::string::npos)
		{
			result += input.substr(offset);
			break;
		}
		else
		{
			result += input.substr(offset, macro_beg - offset);

			if (macro_end == macro_beg + 1) // Handle case of %% to escape percentage symbol
			{
				result += '%';
				continue;
			}
		}

		std::string_view replacing(input);
		replacing = replacing.substr(macro_beg + 1, macro_end - (macro_beg + 1));
		size_t colon_pos = replacing.find(':');

		std::string name;
		if (colon_pos == std::string_view::npos)
			name = replacing;
		else
			name = replacing.substr(0, colon_pos);

		std::string value;
		for (const std::pair<std::string, std::string> &macro : macros)
		{
			if (_stricmp(name.c_str(), macro.first.c_str()) == 0)
			{
				value = macro.second;
				break;
			}
		}

		if (colon_pos == std::string_view::npos)
		{
			result += value;
		}
		else
		{
			std::string_view param = replacing.substr(colon_pos + 1);

			if (const size_t insert_pos = param.find('$');
				insert_pos != std::string_view::npos)
			{
				result += param.substr(0, insert_pos);
				result += value;
				result += param.substr(insert_pos + 1);
			}
			else
			{
				result += param;
			}
		}
	}

	return result;
}

bool reshade::runtime::get_texture_data(api::resource resource, api::resource_usage state, uint8_t *pixels)
{
	const api::resource_desc desc = _device->get_resource_desc(resource);
	const api::format view_format = api::format_to_default_typed(desc.texture.format, 0);

	if (view_format != api::format::r8_unorm &&
		view_format != api::format::r8g8_unorm &&
		view_format != api::format::r8g8b8a8_unorm &&
		view_format != api::format::b8g8r8a8_unorm &&
		view_format != api::format::r8g8b8x8_unorm &&
		view_format != api::format::b8g8r8x8_unorm &&
		view_format != api::format::r10g10b10a2_unorm &&
		view_format != api::format::b10g10r10a2_unorm)
	{
		log::message(log::level::error, "Screenshots are not supported for format %u!", static_cast<uint32_t>(desc.texture.format));
		return false;
	}

	// Copy back buffer data into system memory buffer
	api::resource intermediate;
	if (!_device->create_resource(api::resource_desc(desc.texture.width, desc.texture.height, 1, 1, view_format, 1, api::memory_heap::gpu_to_cpu, api::resource_usage::copy_dest), nullptr, api::resource_usage::copy_dest, &intermediate))
	{
		log::message(log::level::error, "Failed to create system memory texture for screenshot capture!");
		return false;
	}

	_device->set_resource_name(intermediate, "ReShade screenshot texture");

	api::command_list *const cmd_list = _graphics_queue->get_immediate_command_list();
	cmd_list->barrier(resource, state, api::resource_usage::copy_source);
	cmd_list->copy_texture_region(resource, 0, nullptr, intermediate, 0, nullptr);
	cmd_list->barrier(resource, api::resource_usage::copy_source, state);

	api::fence copy_sync_fence = {};
	if (!_device->create_fence(0, api::fence_flags::none, &copy_sync_fence) || !_graphics_queue->signal(copy_sync_fence, 1) || !_device->wait(copy_sync_fence, 1))
		_graphics_queue->wait_idle();
	_device->destroy_fence(copy_sync_fence);

	// Copy data from intermediate image into output buffer
	api::subresource_data mapped_data = {};
	if (_device->map_texture_region(intermediate, 0, nullptr, api::map_access::read_only, &mapped_data))
	{
		auto mapped_pixels = static_cast<const uint8_t *>(mapped_data.data);
		const uint32_t pixels_row_pitch = desc.texture.width * 4;

		for (size_t y = 0; y < desc.texture.height; ++y, pixels += pixels_row_pitch, mapped_pixels += mapped_data.row_pitch)
		{
			switch (view_format)
			{
			case api::format::r8_unorm:
				for (size_t x = 0; x < desc.texture.width; ++x)
				{
					pixels[x * 4 + 0] = mapped_pixels[x];
					pixels[x * 4 + 1] = 0;
					pixels[x * 4 + 2] = 0;
					pixels[x * 4 + 3] = 0xFF;
				}
				break;
			case api::format::r8g8_unorm:
				for (size_t x = 0; x < desc.texture.width; ++x)
				{
					pixels[x * 4 + 0] = mapped_pixels[x * 2 + 0];
					pixels[x * 4 + 1] = mapped_pixels[x * 2 + 1];
					pixels[x * 4 + 2] = 0;
					pixels[x * 4 + 3] = 0xFF;
				}
				break;
			case api::format::r8g8b8a8_unorm:
			case api::format::r8g8b8x8_unorm:
				std::memcpy(pixels, mapped_pixels, pixels_row_pitch);
				if (view_format == api::format::r8g8b8x8_unorm)
					for (size_t x = 0; x < pixels_row_pitch; x += 4)
						pixels[x + 3] = 0xFF;
				break;
			case api::format::b8g8r8a8_unorm:
			case api::format::b8g8r8x8_unorm:
				std::memcpy(pixels, mapped_pixels, pixels_row_pitch);
				// Format is BGRA, but output should be RGBA, so flip channels
				for (size_t x = 0; x < pixels_row_pitch; x += 4)
					std::swap(pixels[x + 0], pixels[x + 2]);
				if (view_format == api::format::b8g8r8x8_unorm)
					for (size_t x = 0; x < pixels_row_pitch; x += 4)
						pixels[x + 3] = 0xFF;
				break;
			case api::format::r10g10b10a2_unorm:
			case api::format::b10g10r10a2_unorm:
				for (size_t x = 0; x < pixels_row_pitch; x += 4)
				{
					const uint32_t rgba = *reinterpret_cast<const uint32_t *>(mapped_pixels + x);
					// Divide by 4 to get 10-bit range (0-1023) into 8-bit range (0-255)
					pixels[x + 0] = (( rgba & 0x000003FF)        /  4) & 0xFF;
					pixels[x + 1] = (((rgba & 0x000FFC00) >> 10) /  4) & 0xFF;
					pixels[x + 2] = (((rgba & 0x3FF00000) >> 20) /  4) & 0xFF;
					pixels[x + 3] = (((rgba & 0xC0000000) >> 30) * 85) & 0xFF;
					if (view_format == api::format::b10g10r10a2_unorm)
						std::swap(pixels[x + 0], pixels[x + 2]);
				}
				break;
			}
		}

		_device->unmap_texture_region(intermediate, 0);
	}

	_device->destroy_resource(intermediate);

	return mapped_data.data != nullptr;
}
