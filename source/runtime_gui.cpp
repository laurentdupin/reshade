/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if RESHADE_GUI

#include "runtime.hpp"
#include "runtime_internal.hpp"
#include "version.h"
#include "dll_log.hpp"
#include "dll_resources.hpp"
#include "input.hpp"
#include "imgui_widgets.hpp"
#include "platform_utils.hpp"
#include "fonts/forkawesome.inl"
#include "fonts/glyph_ranges.hpp"
#include <cmath> // std::abs, std::ceil, std::floor
#include <cctype> // std::tolower
#include <cstdlib> // std::lldiv, std::strtol
#include <cstring> // std::memcmp, std::memcpy
#include <algorithm> // std::any_of, std::count_if, std::find, std::find_if, std::max, std::min, std::replace, std::rotate, std::search, std::swap, std::transform

#include <nlohmann/json.hpp>
#include <locale>
#include <codecvt>
#include <sstream>

using json = nlohmann::json;

static bool filter_text(const std::string_view text, const std::string_view filter)
{
	return filter.empty() ||
		std::search(text.cbegin(), text.cend(), filter.cbegin(), filter.cend(),
			[](const char c1, const char c2) { // Search case-insensitive
				return (('a' <= c1 && c1 <= 'z') ? static_cast<char>(c1 - ' ') : c1) == (('a' <= c2 && c2 <= 'z') ? static_cast<char>(c2 - ' ') : c2);
			}) != text.cend();
}
static auto filter_name(ImGuiInputTextCallbackData *data) -> int
{
	// A file name cannot contain any of the following characters
	return data->EventChar == L'\"' || data->EventChar == L'*' || data->EventChar == L'/' || data->EventChar == L':' || data->EventChar == L'<' || data->EventChar == L'>' || data->EventChar == L'?' || data->EventChar == L'\\' || data->EventChar == L'|';
}

template <typename F>
static void parse_errors(const std::string_view errors, F &&callback)
{
	for (size_t offset = 0, next; offset != std::string_view::npos; offset = next)
	{
		const size_t pos_error = errors.find(": ", offset);
		const size_t pos_error_line = errors.rfind('(', pos_error); // Paths can contain '(', but no ": ", so search backwards from the error location to find the line info
		if (pos_error == std::string_view::npos || pos_error_line == std::string_view::npos || pos_error_line < offset)
			break;

		const size_t pos_linefeed = errors.find('\n', pos_error);

		next = pos_linefeed != std::string_view::npos ? pos_linefeed + 1 : std::string_view::npos;

		const std::string_view error_file = errors.substr(offset, pos_error_line - offset);
		int error_line = static_cast<int>(std::strtol(errors.data() + pos_error_line + 1, nullptr, 10));
		const std::string_view error_text = errors.substr(pos_error + 2 /* skip space */, pos_linefeed - pos_error - 2);

		callback(error_file, error_line, error_text);
	}
}

template <typename T>
static std::string_view get_localized_annotation(T &object, const std::string_view ann_name, [[maybe_unused]] std::string language)
{
	return object.annotation_as_string(ann_name);
}

static const ImVec4 COLOR_RED = ImColor(240, 100, 100);
static const ImVec4 COLOR_YELLOW = ImColor(204, 204, 0);

void reshade::runtime::init_gui()
{
	// Default shortcut: Home
	_overlay_key_data[0] = 0x24;
	_overlay_key_data[1] = false;
	_overlay_key_data[2] = false;
	_overlay_key_data[3] = false;

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	_imgui_context = ImGui::CreateContext();

	ImGuiIO &imgui_io = _imgui_context->IO;
	imgui_io.IniFilename = nullptr;
	imgui_io.ConfigFlags = ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset;

	ImGuiStyle &imgui_style = _imgui_context->Style;
	// Disable rounding by default
	imgui_style.GrabRounding = 0.0f;
	imgui_style.FrameRounding = 0.0f;
	imgui_style.ChildRounding = 0.0f;
	imgui_style.ScrollbarRounding = 0.0f;
	imgui_style.WindowRounding = 0.0f;
	imgui_style.WindowBorderSize = 0.0f;

	// Restore previous context in case this was called from a new runtime being created from an add-on event triggered by an existing runtime
	ImGui::SetCurrentContext(backup_context);
}
void reshade::runtime::deinit_gui()
{
	ImGui::DestroyContext(_imgui_context);
	CloseTranslationDataBuffers();

	ExtractedTexts.clear();
	TranslatedTexts.clear();
}

void reshade::runtime::build_font_atlas()
{
	ImFontAtlas *const atlas = _imgui_context->IO.Fonts;

	if (atlas->IsBuilt())
		return;

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	// Remove any existing fonts from atlas first
	atlas->Clear();

	atlas->TexDesiredWidth = 8192;

	std::error_code ec;
	const ImWchar *glyph_ranges = nullptr;
	std::filesystem::path resolved_font_path;

	static ImFontGlyphRangesBuilder range;
	range.Clear();
	static ImVector<ImWchar> gr;
	gr.clear();

	range.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
	range.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
	range.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesJapanese());

	range.BuildRanges(&gr);

	{
		glyph_ranges = gr.Data;
	}

	const auto add_font_from_file = [atlas](std::filesystem::path &font_path, ImFontConfig cfg, const ImWchar *glyph_ranges, std::error_code &ec) -> bool {
		if (font_path.empty())
			return true;

		extern bool resolve_path(std::filesystem::path &path, std::error_code &ec);
		if (!resolve_path(font_path, ec))
			return false;

		if (FILE *const file = _wfsopen(font_path.c_str(), L"rb", SH_DENYNO))
		{
			fseek(file, 0, SEEK_END);
			const size_t data_size = ftell(file);
			fseek(file, 0, SEEK_SET);

			void *data = IM_ALLOC(data_size);
			const size_t data_size_read = fread(data, 1, data_size, file);
			fclose(file);
			if (data_size_read != data_size)
			{
				IM_FREE(data);
				return false;
			}

			ImFormatString(cfg.Name, IM_ARRAYSIZE(cfg.Name), "%s, %.0fpx", font_path.stem().u8string().c_str(), cfg.SizePixels);

			return atlas->AddFontFromMemoryTTF(data, static_cast<int>(data_size), cfg.SizePixels, &cfg, glyph_ranges) != nullptr;
		}

		return false;
	};

	ImFontConfig cfg;
	cfg.GlyphOffset.y = std::floor(_font_size / 13.0f); // Not used in AddFontDefault()
	cfg.SizePixels = static_cast<float>(_font_size);

	// Add main font
	resolved_font_path = "C:\\Users\\Frere\\AppData\\Local\\Microsoft\\Windows\\Fonts\\NotoSansCJKsc-Medium.ttf";

	{
		if (!add_font_from_file(resolved_font_path, cfg, glyph_ranges, ec))
		{
			log::message(log::level::error, "Failed to load font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());
			resolved_font_path.clear();
		}

		// Use default font if custom font failed to load
		if (resolved_font_path.empty())
			atlas->AddFontDefault(&cfg);

		// Merge icons into main font
		cfg.MergeMode = true;
		cfg.PixelSnapH = true;

		// This need to be static so that it doesn't fall out of scope before the atlas is built below
		static constexpr ImWchar icon_ranges[] = { ICON_MIN_FK, ICON_MAX_FK, 0 }; // Zero-terminated list

		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, cfg.SizePixels, &cfg, icon_ranges);
	}

	if (atlas->Build())
	{
#if RESHADE_VERBOSE_LOG
		log::message(log::level::debug, "Font atlas size: %dx%d", atlas->TexWidth, atlas->TexHeight);
#endif
	}
	else
	{
		log::message(log::level::error, "Failed to build font atlas!");

		atlas->Clear();

		// If unable to build font atlas due to an invalid custom font, revert to the default font
		for (int i = 0; i < (_editor_font_size != _font_size ? 2 : 1); ++i)
		{
			cfg = ImFontConfig();
			cfg.SizePixels = static_cast<float>(i == 0 ? _font_size : _editor_font_size);

			atlas->AddFontDefault(&cfg);
		}
	}

	ImGui::SetCurrentContext(backup_context);

	_show_splash = true;

	int width, height;
	unsigned char *pixels;
	// This will also build the font atlas again if that previously failed above
	atlas->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Make sure font atlas is not currently in use before destroying it
	_graphics_queue->wait_idle();

	_device->destroy_resource(_font_atlas_tex);
	_font_atlas_tex = {};
	_device->destroy_resource_view(_font_atlas_srv);
	_font_atlas_srv = {};

	const api::subresource_data initial_data = { pixels, static_cast<uint32_t>(width * 4), static_cast<uint32_t>(width * height * 4) };

	// Create font atlas texture and upload it
	if (!_device->create_resource(
			api::resource_desc(width, height, 1, 1, api::format::r8g8b8a8_unorm, 1, api::memory_heap::gpu_only, api::resource_usage::shader_resource),
			&initial_data, api::resource_usage::shader_resource, &_font_atlas_tex))
	{
		log::message(log::level::error, "Failed to create front atlas resource!");
		return;
	}

	// Texture data is now uploaded, so can free the memory
	atlas->ClearTexData();

	if (!_device->create_resource_view(_font_atlas_tex, api::resource_usage::shader_resource, api::resource_view_desc(api::format::r8g8b8a8_unorm), &_font_atlas_srv))
	{
		log::message(log::level::error, "Failed to create font atlas resource view!");
		return;
	}

	_device->set_resource_name(_font_atlas_tex, "ImGui font atlas");
}

#define TRANSLATION_AND_EXTRACTION_BUFFER_SIZE 1000000

void reshade::runtime::CheckAndOpenTranslationDataBuffers()
{
	if (_hMapFileExtraction == NULL)
	{
		_hMapFileExtraction = OpenFileMapping(PAGE_READWRITE, FALSE, L"TextExtractionOutputDataMemory");

		if (_hMapFileExtraction != NULL)
		{
			_pBufExtraction = (LPTSTR)MapViewOfFile(_hMapFileExtraction, FILE_MAP_READ, 0, 0, 0);

			if (_pBufExtraction == NULL)
			{
				CloseHandle(_hMapFileExtraction);
				_hMapFileExtraction = NULL;
			}
		}
	}

	if (_hMapFileExtractionHeader == NULL)
	{
		_hMapFileExtractionHeader = OpenFileMapping(PAGE_READWRITE, FALSE, L"TextExtractionOutputHeaderMemory");

		if (_hMapFileExtractionHeader != NULL)
		{
			_pBufExtractionHeader = (LPTSTR)MapViewOfFile(_hMapFileExtractionHeader, FILE_MAP_READ, 0, 0, 0);

			if (_pBufExtractionHeader == NULL)
			{
				CloseHandle(_hMapFileExtractionHeader);
				_hMapFileExtractionHeader = NULL;
			}
		}
	}

	if (_MutexExtraction == NULL)
	{
		_MutexExtraction = CreateMutex(NULL, FALSE, L"TextExtractionOutputMutex");
	}

	if (_hMapFileTranslation == NULL)
	{
		_hMapFileTranslation = OpenFileMapping(PAGE_READWRITE, FALSE, L"TranslationOutputDataMemory");

		if (_hMapFileTranslation != NULL)
		{
			_pBufTranslation = (LPTSTR)MapViewOfFile(_hMapFileTranslation, FILE_MAP_READ, 0, 0, 0);

			if (_pBufTranslation == NULL)
			{
				CloseHandle(_hMapFileTranslation);
				_hMapFileTranslation = NULL;
			}
		}
	}

	if (_hMapFileTranslationHeader == NULL)
	{
		_hMapFileTranslationHeader = OpenFileMapping(PAGE_READWRITE, FALSE, L"TranslationOutputHeaderMemory");

		if (_hMapFileTranslationHeader != NULL)
		{
			_pBufTranslationHeader = (LPTSTR)MapViewOfFile(_hMapFileTranslationHeader, FILE_MAP_READ, 0, 0, 0);

			if (_pBufTranslationHeader == NULL)
			{
				CloseHandle(_hMapFileTranslationHeader);
				_hMapFileTranslationHeader = NULL;
			}
		}
	}

	if (_MutexTranslation == NULL)
	{
		_MutexTranslation = CreateMutex(NULL, FALSE, L"TranslationOutputMutex");
	}
}

static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> UTF8Converter;

std::string ToUTF8(uint32_t cp)
{
	return UTF8Converter.to_bytes( (char32_t)cp );
}

void ConvertUnicodeEscapedToUtf8(std::string &str)
{
	std::string::size_type startIdx = 0;
	do
	{
		startIdx = str.find("\\u", startIdx);
		if (startIdx == std::string::npos) break;

		std::string::size_type endIdx = str.find_first_not_of("0123456789abcdefABCDEF", startIdx + 2);
		if (endIdx == std::string::npos) break;

		if (endIdx > startIdx + 6)
		{
			endIdx = startIdx + 6;
		}

		std::string tmpStr = str.substr(startIdx + 2, endIdx - (startIdx + 2));
		std::istringstream iss(tmpStr);

		uint32_t cp;
		if (iss >> std::hex >> cp)
		{
			std::string utf8 = ToUTF8(cp);
			str.replace(startIdx, 2 + tmpStr.length(), utf8);
			startIdx += utf8.length();
		}
		else
			startIdx += 2;
	} while (true);
}

void reshade::runtime::UpdateTranslationData()
{
	char *ReceivingBuffer = new char[TRANSLATION_AND_EXTRACTION_BUFFER_SIZE];

	if (_pBufExtraction != NULL && _MutexExtraction != NULL && _pBufExtractionHeader != NULL)
	{
		WaitForSingleObject(_MutexExtraction, INFINITE);
		memcpy_s(ReceivingBuffer, TRANSLATION_AND_EXTRACTION_BUFFER_SIZE, (char *)_pBufExtraction, ((StandardHeader*)_pBufExtractionHeader)->Size);
		ReceivingBuffer[((StandardHeader *)_pBufExtractionHeader)->Size] = '\0';
		ReleaseMutex(_MutexExtraction);

		auto BufferedString = std::string(ReceivingBuffer);
		ConvertUnicodeEscapedToUtf8(BufferedString);

		if (strlen(ReceivingBuffer) > 0)
		{
			try
			{
				auto extraction = json::parse(BufferedString);

				ExtractedTexts.clear();

				for (auto &extract : extraction)
				{
					auto &back = ExtractedTexts.emplace_back();
					back.TopLeftCornerX = extract["TopLeftCornerX"];
					back.TopLeftCornerY = extract["TopLeftCornerY"];
					back.BottomRightCornerX = extract["BottomRightCornerX"];
					back.BottomRightCornerY = extract["BottomRightCornerY"];
					back.Text = extract["Text"];
				}
			}
			catch (const std::exception &)
			{
				
			}
		}
	}

	if (_pBufTranslation != NULL && _MutexTranslation != NULL && _pBufTranslationHeader != NULL)
	{
		WaitForSingleObject(_MutexTranslation, INFINITE);
		memcpy_s(ReceivingBuffer, TRANSLATION_AND_EXTRACTION_BUFFER_SIZE, (char *)_pBufTranslation, ((StandardHeader *)_pBufTranslationHeader)->Size);
		ReceivingBuffer[((StandardHeader *)_pBufTranslationHeader)->Size] = '\0';
		ReleaseMutex(_MutexTranslation);

		if (strlen(ReceivingBuffer) > 0)
		{
			try
			{
				auto translation = json::parse(ReceivingBuffer);

				TranslatedTexts.clear();

				for (auto &translate : translation.items())
				{
					TranslatedTexts.try_emplace(translate.key(), translate.value());
				}
			}
			catch (const std::exception &)
			{

			}
		}
	}

	delete[] ReceivingBuffer;
}

void reshade::runtime::CloseTranslationDataBuffers()
{
	if (_pBufExtraction != NULL)
	{
		UnmapViewOfFile(_pBufExtraction);
		_pBufExtraction = NULL;
	}

	if (_hMapFileExtraction != NULL)
	{
		CloseHandle(_hMapFileExtraction);
		_hMapFileExtraction = NULL;
	}

	if (_pBufExtractionHeader != NULL)
	{
		UnmapViewOfFile(_pBufExtractionHeader);
		_pBufExtractionHeader = NULL;
	}

	if (_hMapFileExtractionHeader != NULL)
	{
		CloseHandle(_hMapFileExtractionHeader);
		_hMapFileExtractionHeader = NULL;
	}

	if (_MutexExtraction != NULL)
	{
		CloseHandle(_MutexExtraction);
		_MutexExtraction = NULL;
	}

	if (_pBufTranslation != NULL)
	{
		UnmapViewOfFile(_pBufTranslation);
		_pBufTranslation = NULL;
	}

	if (_hMapFileTranslation != NULL)
	{
		CloseHandle(_hMapFileTranslation);
		_hMapFileTranslation = NULL;
	}

	if (_pBufTranslationHeader != NULL)
	{
		UnmapViewOfFile(_pBufTranslationHeader);
		_pBufTranslationHeader = NULL;
	}

	if (_hMapFileTranslation != NULL)
	{
		CloseHandle(_hMapFileTranslation);
		_hMapFileTranslation = NULL;
	}

	if (_MutexTranslation != NULL)
	{
		CloseHandle(_MutexTranslation);
		_MutexTranslation = NULL;
	}
}

void reshade::runtime::draw_gui()
{
	assert(_is_initialized);

	CheckAndOpenTranslationDataBuffers();
	UpdateTranslationData();

	_show_overlay = true;
	api::input_source show_overlay_source = api::input_source::keyboard;

	_ignore_shortcuts = false;
	_block_input_next_frame = false;

	_input->block_mouse_input(false);
	_input->block_keyboard_input(false);

	if (!_show_overlay)
	{
		if (_input != nullptr)
		{
			_input->block_mouse_input(false);
			_input->block_keyboard_input(false);
		}
		return; // Early-out to avoid costly ImGui calls when no GUI elements are on the screen
	}

	build_font_atlas();
	if (_font_atlas_srv == 0)
		return; // Cannot render GUI without font atlas

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	ImGuiIO &imgui_io = _imgui_context->IO;
	imgui_io.DeltaTime = _last_frame_duration.count() * 1e-9f;
	imgui_io.DisplaySize.x = static_cast<float>(_width);
	imgui_io.DisplaySize.y = static_cast<float>(_height);
	imgui_io.Fonts->TexID = _font_atlas_srv.handle;

	if (_input != nullptr)
	{
		// Scale mouse position in case render resolution does not match the window size
		unsigned int max_position[2];
		_input->max_mouse_position(max_position);

		imgui_io.AddMousePosEvent(
			_input->mouse_position_x() * (imgui_io.DisplaySize.x / max_position[0]),
			_input->mouse_position_y() * (imgui_io.DisplaySize.y / max_position[1]));

		// Add wheel delta to the current absolute mouse wheel position
		imgui_io.AddMouseWheelEvent(0.0f, _input->mouse_wheel_delta());

		// Update all the button states
		constexpr std::pair<ImGuiKey, unsigned int> key_mappings[] = {
			{ ImGuiKey_Tab, 0x09 /* VK_TAB */ },
			{ ImGuiKey_LeftArrow, 0x25 /* VK_LEFT */ },
			{ ImGuiKey_RightArrow, 0x27 /* VK_RIGHT */ },
			{ ImGuiKey_UpArrow, 0x26 /* VK_UP */ },
			{ ImGuiKey_DownArrow, 0x28 /* VK_DOWN */ },
			{ ImGuiKey_PageUp, 0x21 /* VK_PRIOR */ },
			{ ImGuiKey_PageDown, 0x22 /* VK_NEXT */ },
			{ ImGuiKey_End, 0x23 /* VK_END */ },
			{ ImGuiKey_Home, 0x24 /* VK_HOME */ },
			{ ImGuiKey_Insert, 0x2D /* VK_INSERT */ },
			{ ImGuiKey_Delete, 0x2E /* VK_DELETE */ },
			{ ImGuiKey_Backspace, 0x08 /* VK_BACK */ },
			{ ImGuiKey_Space, 0x20 /* VK_SPACE */ },
			{ ImGuiKey_Enter, 0x0D /* VK_RETURN */ },
			{ ImGuiKey_Escape, 0x1B /* VK_ESCAPE */ },
			{ ImGuiKey_LeftCtrl, 0xA2 /* VK_LCONTROL */ },
			{ ImGuiKey_LeftShift, 0xA0 /* VK_LSHIFT */ },
			{ ImGuiKey_LeftAlt, 0xA4 /* VK_LMENU */ },
			{ ImGuiKey_LeftSuper, 0x5B /* VK_LWIN */ },
			{ ImGuiKey_RightCtrl, 0xA3 /* VK_RCONTROL */ },
			{ ImGuiKey_RightShift, 0xA1 /* VK_RSHIFT */ },
			{ ImGuiKey_RightAlt, 0xA5 /* VK_RMENU */ },
			{ ImGuiKey_RightSuper, 0x5C /* VK_RWIN */ },
			{ ImGuiKey_Menu, 0x5D /* VK_APPS */ },
			{ ImGuiKey_0, '0' },
			{ ImGuiKey_1, '1' },
			{ ImGuiKey_2, '2' },
			{ ImGuiKey_3, '3' },
			{ ImGuiKey_4, '4' },
			{ ImGuiKey_5, '5' },
			{ ImGuiKey_6, '6' },
			{ ImGuiKey_7, '7' },
			{ ImGuiKey_8, '8' },
			{ ImGuiKey_9, '9' },
			{ ImGuiKey_A, 'A' },
			{ ImGuiKey_B, 'B' },
			{ ImGuiKey_C, 'C' },
			{ ImGuiKey_D, 'D' },
			{ ImGuiKey_E, 'E' },
			{ ImGuiKey_F, 'F' },
			{ ImGuiKey_G, 'G' },
			{ ImGuiKey_H, 'H' },
			{ ImGuiKey_I, 'I' },
			{ ImGuiKey_J, 'J' },
			{ ImGuiKey_K, 'K' },
			{ ImGuiKey_L, 'L' },
			{ ImGuiKey_M, 'M' },
			{ ImGuiKey_N, 'N' },
			{ ImGuiKey_O, 'O' },
			{ ImGuiKey_P, 'P' },
			{ ImGuiKey_Q, 'Q' },
			{ ImGuiKey_R, 'R' },
			{ ImGuiKey_S, 'S' },
			{ ImGuiKey_T, 'T' },
			{ ImGuiKey_U, 'U' },
			{ ImGuiKey_V, 'V' },
			{ ImGuiKey_W, 'W' },
			{ ImGuiKey_X, 'X' },
			{ ImGuiKey_Y, 'Y' },
			{ ImGuiKey_Z, 'Z' },
			{ ImGuiKey_F1, 0x70 /* VK_F1 */ },
			{ ImGuiKey_F2, 0x71 /* VK_F2 */ },
			{ ImGuiKey_F3, 0x72 /* VK_F3 */ },
			{ ImGuiKey_F4, 0x73 /* VK_F4 */ },
			{ ImGuiKey_F5, 0x74 /* VK_F5 */ },
			{ ImGuiKey_F6, 0x75 /* VK_F6 */ },
			{ ImGuiKey_F7, 0x76 /* VK_F7 */ },
			{ ImGuiKey_F8, 0x77 /* VK_F8 */ },
			{ ImGuiKey_F9, 0x78 /* VK_F9 */ },
			{ ImGuiKey_F10, 0x79 /* VK_F10 */ },
			{ ImGuiKey_F11, 0x80 /* VK_F11 */ },
			{ ImGuiKey_F12, 0x81 /* VK_F12 */ },
			{ ImGuiKey_Apostrophe, 0xDE /* VK_OEM_7 */ },
			{ ImGuiKey_Comma, 0xBC /* VK_OEM_COMMA */ },
			{ ImGuiKey_Minus, 0xBD /* VK_OEM_MINUS */ },
			{ ImGuiKey_Period, 0xBE /* VK_OEM_PERIOD */ },
			{ ImGuiKey_Slash, 0xBF /* VK_OEM_2 */ },
			{ ImGuiKey_Semicolon, 0xBA /* VK_OEM_1 */ },
			{ ImGuiKey_Equal, 0xBB /* VK_OEM_PLUS */ },
			{ ImGuiKey_LeftBracket, 0xDB /* VK_OEM_4 */ },
			{ ImGuiKey_Backslash, 0xDC /* VK_OEM_5 */ },
			{ ImGuiKey_RightBracket, 0xDD /* VK_OEM_6 */ },
			{ ImGuiKey_GraveAccent, 0xC0 /* VK_OEM_3 */ },
			{ ImGuiKey_CapsLock, 0x14 /* VK_CAPITAL */ },
			{ ImGuiKey_ScrollLock, 0x91 /* VK_SCROLL */ },
			{ ImGuiKey_NumLock, 0x90 /* VK_NUMLOCK */ },
			{ ImGuiKey_PrintScreen, 0x2C /* VK_SNAPSHOT */ },
			{ ImGuiKey_Pause, 0x13 /* VK_PAUSE */ },
			{ ImGuiKey_Keypad0, 0x60 /* VK_NUMPAD0 */ },
			{ ImGuiKey_Keypad1, 0x61 /* VK_NUMPAD1 */ },
			{ ImGuiKey_Keypad2, 0x62 /* VK_NUMPAD2 */ },
			{ ImGuiKey_Keypad3, 0x63 /* VK_NUMPAD3 */ },
			{ ImGuiKey_Keypad4, 0x64 /* VK_NUMPAD4 */ },
			{ ImGuiKey_Keypad5, 0x65 /* VK_NUMPAD5 */ },
			{ ImGuiKey_Keypad6, 0x66 /* VK_NUMPAD6 */ },
			{ ImGuiKey_Keypad7, 0x67 /* VK_NUMPAD7 */ },
			{ ImGuiKey_Keypad8, 0x68 /* VK_NUMPAD8 */ },
			{ ImGuiKey_Keypad9, 0x69 /* VK_NUMPAD9 */ },
			{ ImGuiKey_KeypadDecimal, 0x6E /* VK_DECIMAL */ },
			{ ImGuiKey_KeypadDivide, 0x6F /* VK_DIVIDE */ },
			{ ImGuiKey_KeypadMultiply, 0x6A /* VK_MULTIPLY */ },
			{ ImGuiKey_KeypadSubtract, 0x6D /* VK_SUBTRACT */ },
			{ ImGuiKey_KeypadAdd, 0x6B /* VK_ADD */ },
			{ ImGuiMod_Ctrl, 0x11 /* VK_CONTROL */ },
			{ ImGuiMod_Shift, 0x10 /* VK_SHIFT */ },
			{ ImGuiMod_Alt, 0x12 /* VK_MENU */ },
			{ ImGuiMod_Super, 0x5D /* VK_APPS */ },
		};

		for (const std::pair<ImGuiKey, unsigned int> &mapping : key_mappings)
			imgui_io.AddKeyEvent(mapping.first, _input->is_key_down(mapping.second));
		for (ImGuiMouseButton i = 0; i < ImGuiMouseButton_COUNT; i++)
			imgui_io.AddMouseButtonEvent(i, _input->is_mouse_button_down(i));
		for (ImWchar16 c : _input->text_input())
			imgui_io.AddInputCharacterUTF16(c);
	}

	ImGui::NewFrame();

	ImVec2 viewport_offset = ImVec2(0, 0);
	const bool show_spinner = false;

	ImGui::ShowMetricsWindow();

	// Create ImGui widgets and windows
	if (true)
	{
		if (ExtractedTexts.size() > 0)
		{
			for (auto &text : ExtractedTexts)
			{
				ImGui::SetNextWindowPos(ImVec2(imgui_io.DisplaySize.x * text.TopLeftCornerX, imgui_io.DisplaySize.y * text.TopLeftCornerY), 0, ImVec2(0.0f, 1.0f));
				ImGui::SetNextWindowSize(ImVec2(imgui_io.DisplaySize.x * (text.BottomRightCornerX - text.TopLeftCornerX), imgui_io.DisplaySize.y * (text.TopLeftCornerY - text.BottomRightCornerY)));

				ImGui::Begin(text.Text.c_str(), nullptr,
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoNav |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoInputs |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoFocusOnAppearing);

				{
					if (TranslatedTexts.find(text.Text) != TranslatedTexts.end())
					{
						ImGui::Text(TranslatedTexts.at(text.Text).c_str());
					}
					else
					{
						ImGui::Text(text.Text.c_str());
					}
				}

				ImGui::End();
			}
		}
		else
		{
			ImGui::SetNextWindowPos(ImVec2(imgui_io.DisplaySize.x * 0.5f, imgui_io.DisplaySize.y * 0.5f), NULL, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(100.0f, 50.0f));

			ImGui::Begin("No connection", nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoFocusOnAppearing);

			{
				ImGui::Text("No connection");
			}

			ImGui::End();
		}
	}

	// Disable keyboard shortcuts while typing into input boxes
	_ignore_shortcuts |= ImGui::IsAnyItemActive();

	// Render ImGui widgets and windows
	ImGui::Render();

	if (ImDrawData *const draw_data = ImGui::GetDrawData();
		draw_data != nullptr && draw_data->CmdListsCount != 0 && draw_data->TotalVtxCount != 0)
	{
		api::command_list *const cmd_list = _graphics_queue->get_immediate_command_list();

		if (_back_buffer_resolved != 0)
		{
			render_imgui_draw_data(cmd_list, draw_data, _back_buffer_targets[0]);
		}
		else
		{
			uint32_t back_buffer_index = get_current_back_buffer_index() * 2;
			const api::resource back_buffer_resource = _device->get_resource_from_view(_back_buffer_targets[back_buffer_index]);

			cmd_list->barrier(back_buffer_resource, api::resource_usage::present, api::resource_usage::render_target);
			render_imgui_draw_data(cmd_list, draw_data, _back_buffer_targets[back_buffer_index]);
			cmd_list->barrier(back_buffer_resource, api::resource_usage::render_target, api::resource_usage::present);
		}
	}

	ImGui::SetCurrentContext(backup_context);
}

bool reshade::runtime::init_imgui_resources()
{
	// Adjust default font size based on the vertical resolution
	if (_font_size == 0)
		_editor_font_size = _font_size = _height >= 2160 ? 26 : _height >= 1440 ? 20 : 13;

	_font_size *= 1.5f;

	const bool has_combined_sampler_and_view = _device->check_capability(api::device_caps::sampler_with_resource_view);

	if (_imgui_sampler_state == 0)
	{
		api::sampler_desc sampler_desc = {};
		sampler_desc.filter = api::filter_mode::min_mag_mip_linear;
		sampler_desc.address_u = api::texture_address_mode::clamp;
		sampler_desc.address_v = api::texture_address_mode::clamp;
		sampler_desc.address_w = api::texture_address_mode::clamp;

		if (!_device->create_sampler(sampler_desc, &_imgui_sampler_state))
		{
			log::message(log::level::error, "Failed to create ImGui sampler object!");
			return false;
		}
	}

	if (_imgui_pipeline_layout == 0)
	{
		uint32_t num_layout_params = 0;
		api::pipeline_layout_param layout_params[3];

		if (has_combined_sampler_and_view)
		{
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::sampler_with_resource_view }; // s0
		}
		else
		{
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::sampler }; // s0
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::shader_resource_view }; // t0
		}

		uint32_t num_push_constants = 16;
		reshade::api::shader_stage shader_stage = api::shader_stage::vertex;

		// Add HDR push constants for possible HDR swap chains
		if (((_renderer_id & 0xB000) == 0xB000 || (_renderer_id & 0xC000) == 0xC000 || (_renderer_id & 0x20000) == 0x20000) &&
			(_back_buffer_format == reshade::api::format::r10g10b10a2_unorm || _back_buffer_format == reshade::api::format::b10g10r10a2_unorm || _back_buffer_format == reshade::api::format::r16g16b16a16_float))
		{
			num_push_constants += 4;
			shader_stage |= api::shader_stage::pixel;
		}

		layout_params[num_layout_params++] = api::constant_range { 0, 0, 0, num_push_constants, shader_stage }; // b0

		if (!_device->create_pipeline_layout(num_layout_params, layout_params, &_imgui_pipeline_layout))
		{
			log::message(log::level::error, "Failed to create ImGui pipeline layout!");
			return false;
		}
	}

	if (_imgui_pipeline != 0)
		return true;

	const bool is_possibe_hdr_swapchain =
		((_renderer_id & 0xB000) == 0xB000 || (_renderer_id & 0xC000) == 0xC000 || (_renderer_id & 0x20000) == 0x20000) &&
		(_back_buffer_format == reshade::api::format::r10g10b10a2_unorm || _back_buffer_format == reshade::api::format::b10g10r10a2_unorm || _back_buffer_format == reshade::api::format::r16g16b16a16_float);

	const resources::data_resource vs_res = resources::load_data_resource(
		_renderer_id >= 0x20000 ? IDR_IMGUI_VS_SPIRV :
		_renderer_id >= 0x10000 ? IDR_IMGUI_VS_GLSL :
		_renderer_id >= 0x0a000 ? IDR_IMGUI_VS_4_0 : IDR_IMGUI_VS_3_0);
	api::shader_desc vs_desc;
	vs_desc.code = vs_res.data;
	vs_desc.code_size = vs_res.data_size;

	const resources::data_resource ps_res = resources::load_data_resource(
		_renderer_id >= 0x20000 ? (!is_possibe_hdr_swapchain ? IDR_IMGUI_PS_SPIRV : IDR_IMGUI_PS_SPIRV_HDR) :
		_renderer_id >= 0x10000 ? IDR_IMGUI_PS_GLSL :
		_renderer_id >= 0x0a000 ? (!is_possibe_hdr_swapchain ? IDR_IMGUI_PS_4_0 : IDR_IMGUI_PS_4_0_HDR) : IDR_IMGUI_PS_3_0);
	api::shader_desc ps_desc;
	ps_desc.code = ps_res.data;
	ps_desc.code_size = ps_res.data_size;

	std::vector<api::pipeline_subobject> subobjects;
	subobjects.push_back({ api::pipeline_subobject_type::vertex_shader, 1, &vs_desc });
	subobjects.push_back({ api::pipeline_subobject_type::pixel_shader, 1, &ps_desc });

	const api::input_element input_layout[3] = {
		{ 0, "POSITION", 0, api::format::r32g32_float,   0, offsetof(ImDrawVert, pos), sizeof(ImDrawVert), 0 },
		{ 1, "TEXCOORD", 0, api::format::r32g32_float,   0, offsetof(ImDrawVert, uv ), sizeof(ImDrawVert), 0 },
		{ 2, "COLOR",    0, api::format::r8g8b8a8_unorm, 0, offsetof(ImDrawVert, col), sizeof(ImDrawVert), 0 }
	};
	subobjects.push_back({ api::pipeline_subobject_type::input_layout, 3, (void *)input_layout });

	api::primitive_topology topology = api::primitive_topology::triangle_list;
	subobjects.push_back({ api::pipeline_subobject_type::primitive_topology, 1, &topology });

	api::blend_desc blend_state;
	blend_state.blend_enable[0] = true;
	blend_state.source_color_blend_factor[0] = api::blend_factor::source_alpha;
	blend_state.dest_color_blend_factor[0] = api::blend_factor::one_minus_source_alpha;
	blend_state.color_blend_op[0] = api::blend_op::add;
	blend_state.source_alpha_blend_factor[0] = api::blend_factor::one;
	blend_state.dest_alpha_blend_factor[0] = api::blend_factor::one_minus_source_alpha;
	blend_state.alpha_blend_op[0] = api::blend_op::add;
	blend_state.render_target_write_mask[0] = 0xF;
	subobjects.push_back({ api::pipeline_subobject_type::blend_state, 1, &blend_state });

	api::rasterizer_desc rasterizer_state;
	rasterizer_state.cull_mode = api::cull_mode::none;
	rasterizer_state.scissor_enable = true;
	subobjects.push_back({ api::pipeline_subobject_type::rasterizer_state, 1, &rasterizer_state });

	api::depth_stencil_desc depth_stencil_state;
	depth_stencil_state.depth_enable = false;
	depth_stencil_state.stencil_enable = false;
	subobjects.push_back({ api::pipeline_subobject_type::depth_stencil_state, 1, &depth_stencil_state });

	// Always choose non-sRGB format variant, since 'render_imgui_draw_data' is called with the non-sRGB render target (see 'draw_gui')
	api::format render_target_format = api::format_to_default_typed(_back_buffer_format, 0);
	subobjects.push_back({ api::pipeline_subobject_type::render_target_formats, 1, &render_target_format });

	if (!_device->create_pipeline(_imgui_pipeline_layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &_imgui_pipeline))
	{
		log::message(log::level::error, "Failed to create ImGui pipeline!");
		return false;
	}

	return true;
}
void reshade::runtime::render_imgui_draw_data(api::command_list *cmd_list, ImDrawData *draw_data, api::resource_view rtv)
{
	// Need to multi-buffer vertex data so not to modify data below when the previous frame is still in flight
	const size_t buffer_index = _frame_count % std::size(_imgui_vertices);

	// Create and grow vertex/index buffers if needed
	if (_imgui_num_indices[buffer_index] < draw_data->TotalIdxCount)
	{
		if (_imgui_indices[buffer_index] != 0)
		{
			_graphics_queue->wait_idle(); // Be safe and ensure nothing still uses this buffer

			_device->destroy_resource(_imgui_indices[buffer_index]);
		}

		const int new_size = draw_data->TotalIdxCount + 10000;
		if (!_device->create_resource(api::resource_desc(new_size * sizeof(ImDrawIdx), api::memory_heap::cpu_to_gpu, api::resource_usage::index_buffer), nullptr, api::resource_usage::cpu_access, &_imgui_indices[buffer_index]))
		{
			log::message(log::level::error, "Failed to create ImGui index buffer!");
			return;
		}

		_device->set_resource_name(_imgui_indices[buffer_index], "ImGui index buffer");

		_imgui_num_indices[buffer_index] = new_size;
	}
	if (_imgui_num_vertices[buffer_index] < draw_data->TotalVtxCount)
	{
		if (_imgui_vertices[buffer_index] != 0)
		{
			_graphics_queue->wait_idle();

			_device->destroy_resource(_imgui_vertices[buffer_index]);
		}

		const int new_size = draw_data->TotalVtxCount + 5000;
		if (!_device->create_resource(api::resource_desc(new_size * sizeof(ImDrawVert), api::memory_heap::cpu_to_gpu, api::resource_usage::vertex_buffer), nullptr, api::resource_usage::cpu_access, &_imgui_vertices[buffer_index]))
		{
			log::message(log::level::error, "Failed to create ImGui vertex buffer!");
			return;
		}

		_device->set_resource_name(_imgui_vertices[buffer_index], "ImGui vertex buffer");

		_imgui_num_vertices[buffer_index] = new_size;
	}

#ifndef NDEBUG
	cmd_list->begin_debug_event("ReShade overlay");
#endif

	if (ImDrawIdx *idx_dst;
		_device->map_buffer_region(_imgui_indices[buffer_index], 0, UINT64_MAX, api::map_access::write_only, reinterpret_cast<void **>(&idx_dst)))
	{
		for (int n = 0; n < draw_data->CmdListsCount; ++n)
		{
			const ImDrawList *const draw_list = draw_data->CmdLists[n];
			std::memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			idx_dst += draw_list->IdxBuffer.Size;
		}

		_device->unmap_buffer_region(_imgui_indices[buffer_index]);
	}
	if (ImDrawVert *vtx_dst;
		_device->map_buffer_region(_imgui_vertices[buffer_index], 0, UINT64_MAX, api::map_access::write_only, reinterpret_cast<void **>(&vtx_dst)))
	{
		for (int n = 0; n < draw_data->CmdListsCount; ++n)
		{
			const ImDrawList *const draw_list = draw_data->CmdLists[n];
			std::memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
			vtx_dst += draw_list->VtxBuffer.Size;
		}

		_device->unmap_buffer_region(_imgui_vertices[buffer_index]);
	}

	api::render_pass_render_target_desc render_target = {};
	render_target.view = rtv;

	cmd_list->begin_render_pass(1, &render_target, nullptr);

	// Setup render state
	cmd_list->bind_pipeline(api::pipeline_stage::all_graphics, _imgui_pipeline);

	cmd_list->bind_index_buffer(_imgui_indices[buffer_index], 0, sizeof(ImDrawIdx));
	cmd_list->bind_vertex_buffer(0, _imgui_vertices[buffer_index], 0, sizeof(ImDrawVert));

	const api::viewport viewport = { 0, 0, draw_data->DisplaySize.x, draw_data->DisplaySize.y, 0.0f, 1.0f };
	cmd_list->bind_viewports(0, 1, &viewport);

	// Setup orthographic projection matrix
	const bool flip_y = (_renderer_id & 0x10000) != 0;
	const bool adjust_half_pixel = _renderer_id < 0xa000; // Bake half-pixel offset into matrix in D3D9
	const bool depth_clip_zero_to_one = (_renderer_id & 0x10000) == 0;

	const float ortho_projection[16] = {
		2.0f / draw_data->DisplaySize.x, 0.0f, 0.0f, 0.0f,
		0.0f, (flip_y ? 2.0f : -2.0f) / draw_data->DisplaySize.y, 0.0f, 0.0f,
		0.0f,                            0.0f, depth_clip_zero_to_one ? 0.5f : -1.0f, 0.0f,
		                   -(2 * draw_data->DisplayPos.x + draw_data->DisplaySize.x + (adjust_half_pixel ? 1.0f : 0.0f)) / draw_data->DisplaySize.x,
		(flip_y ? -1 : 1) * (2 * draw_data->DisplayPos.y + draw_data->DisplaySize.y + (adjust_half_pixel ? 1.0f : 0.0f)) / draw_data->DisplaySize.y, depth_clip_zero_to_one ? 0.5f : 0.0f, 1.0f,
	};

	const bool has_combined_sampler_and_view = _device->check_capability(api::device_caps::sampler_with_resource_view);
	cmd_list->push_constants(api::shader_stage::vertex, _imgui_pipeline_layout, has_combined_sampler_and_view ? 1 : 2, 0, sizeof(ortho_projection) / 4, ortho_projection);
	if (!has_combined_sampler_and_view)
		cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 0, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::sampler, &_imgui_sampler_state });

	// Add HDR push constants for possible HDR swap chains
	if (((_renderer_id & 0xB000) == 0xB000 || (_renderer_id & 0xC000) == 0xC000 || (_renderer_id & 0x20000) == 0x20000) &&
		(_back_buffer_format == reshade::api::format::r10g10b10a2_unorm || _back_buffer_format == reshade::api::format::b10g10r10a2_unorm || _back_buffer_format == reshade::api::format::r16g16b16a16_float))
	{
		const struct {
			api::format back_buffer_format;
			api::color_space back_buffer_color_space;
			float hdr_overlay_brightness;
			api::color_space hdr_overlay_overwrite_color_space;
		} hdr_push_constants = {
			_back_buffer_format,
			_back_buffer_color_space,
			_hdr_overlay_brightness,
			_hdr_overlay_overwrite_color_space
		};

		cmd_list->push_constants(api::shader_stage::pixel, _imgui_pipeline_layout, has_combined_sampler_and_view ? 1 : 2, sizeof(ortho_projection) / 4, sizeof(hdr_push_constants) / 4, &hdr_push_constants);
	}

	int vtx_offset = 0, idx_offset = 0;
	for (int n = 0; n < draw_data->CmdListsCount; ++n)
	{
		const ImDrawList *const draw_list = draw_data->CmdLists[n];

		for (const ImDrawCmd &cmd : draw_list->CmdBuffer)
		{
			if (cmd.UserCallback != nullptr)
			{
				cmd.UserCallback(draw_list, &cmd);
				continue;
			}

			assert(cmd.TextureId != 0);

			const api::rect scissor_rect = {
				static_cast<int32_t>(cmd.ClipRect.x - draw_data->DisplayPos.x),
				flip_y ? static_cast<int32_t>(_height - cmd.ClipRect.w + draw_data->DisplayPos.y) : static_cast<int32_t>(cmd.ClipRect.y - draw_data->DisplayPos.y),
				static_cast<int32_t>(cmd.ClipRect.z - draw_data->DisplayPos.x),
				flip_y ? static_cast<int32_t>(_height - cmd.ClipRect.y + draw_data->DisplayPos.y) : static_cast<int32_t>(cmd.ClipRect.w - draw_data->DisplayPos.y)
			};

			cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

			const api::resource_view srv = { (uint64_t)cmd.TextureId };
			if (has_combined_sampler_and_view)
			{
				api::sampler_with_resource_view sampler_and_view = { _imgui_sampler_state, srv };
				cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 0, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::sampler_with_resource_view, &sampler_and_view });
			}
			else
			{
				cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 1, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::shader_resource_view, &srv });
			}

			cmd_list->draw_indexed(cmd.ElemCount, 1, cmd.IdxOffset + idx_offset, cmd.VtxOffset + vtx_offset, 0);
		}

		idx_offset += draw_list->IdxBuffer.Size;
		vtx_offset += draw_list->VtxBuffer.Size;
	}

	cmd_list->end_render_pass();

#ifndef NDEBUG
	cmd_list->end_debug_event();
#endif
}
void reshade::runtime::destroy_imgui_resources()
{
	_imgui_context->IO.Fonts->Clear();

	_device->destroy_resource(_font_atlas_tex);
	_font_atlas_tex = {};
	_device->destroy_resource_view(_font_atlas_srv);
	_font_atlas_srv = {};

	for (size_t i = 0; i < std::size(_imgui_vertices); ++i)
	{
		_device->destroy_resource(_imgui_indices[i]);
		_imgui_indices[i] = {};
		_imgui_num_indices[i] = 0;
		_device->destroy_resource(_imgui_vertices[i]);
		_imgui_vertices[i] = {};
		_imgui_num_vertices[i] = 0;
	}

	_device->destroy_sampler(_imgui_sampler_state);
	_imgui_sampler_state = {};
	_device->destroy_pipeline(_imgui_pipeline);
	_imgui_pipeline = {};
	_device->destroy_pipeline_layout(_imgui_pipeline_layout);
	_imgui_pipeline_layout = {};
}

#endif
