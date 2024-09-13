/*
 * Copyright (C) 2024 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.hpp"
#include "runtime_manager.hpp"
#include <cassert>
#include <shared_mutex>
#include <unordered_set>
#include <string>

void reshade::create_effect_runtime(api::swapchain *swapchain, api::command_queue *graphics_queue)
{
	if (graphics_queue == nullptr || &swapchain->get_private_data<reshade::runtime>() != nullptr)
		return;

	assert((graphics_queue->get_type() & api::command_queue_type::graphics) != 0);

	// Try to find a unique configuration name for this effect runtime instance
	std::string config_name = "ReShade";
	swapchain->create_private_data<reshade::runtime>(swapchain, graphics_queue);
}
void reshade::destroy_effect_runtime(api::swapchain *swapchain)
{
	swapchain->destroy_private_data<reshade::runtime>();
}

void reshade::init_effect_runtime(api::swapchain *swapchain)
{
	if (const auto runtime = &swapchain->get_private_data<reshade::runtime>())
		runtime->on_init();
}
void reshade::reset_effect_runtime(api::swapchain *swapchain)
{
	if (const auto runtime = &swapchain->get_private_data<reshade::runtime>())
		runtime->on_reset();
}
void reshade::present_effect_runtime(api::swapchain *swapchain, reshade::api::command_queue *present_queue)
{
	if (const auto runtime = &swapchain->get_private_data<reshade::runtime>())
		runtime->on_present(present_queue);
}
