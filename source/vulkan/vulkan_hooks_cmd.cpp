/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "vulkan_hooks.hpp"
#include "vulkan_impl_device.hpp"
#include "vulkan_impl_command_list.hpp"
#include "vulkan_impl_type_convert.hpp"
#include "dll_log.hpp"
#include "lockfree_linear_map.hpp"
#include <cstring> // std::memcpy, std::memset
#include <algorithm> // std::copy_n, std::max, std::min, std::swap

extern lockfree_linear_map<void *, reshade::vulkan::device_impl *, 8> g_vulkan_devices;

#define GET_DISPATCH_PTR_FROM(name, data) \
	assert((data) != nullptr); \
	PFN_vk##name trampoline = (data)->_dispatch_table.name; \
	assert(trampoline != nullptr)



VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(BeginCommandBuffer, device_impl);
	const VkResult result = trampoline(commandBuffer, pBeginInfo);
#if RESHADE_VERBOSE_LOG
	if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkBeginCommandBuffer failed with error code %d.", static_cast<int>(result));
	}
#endif
	return result;
}
VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(EndCommandBuffer, device_impl);
	const VkResult result = trampoline(commandBuffer);
#if RESHADE_VERBOSE_LOG
	if (result < VK_SUCCESS)
	{
		reshade::log::message(reshade::log::level::warning, "vkEndCommandBuffer failed with error code %d.", static_cast<int>(result));
	}
#endif
	return result;
}

void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindPipeline, device_impl);
	trampoline(commandBuffer, pipelineBindPoint, pipeline);

}

void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetViewport, device_impl);
	trampoline(commandBuffer, firstViewport, viewportCount, pViewports);

}
void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetScissor, device_impl);
	trampoline(commandBuffer, firstScissor, scissorCount, pScissors);

}

void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetDepthBias, device_impl);
	trampoline(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);

}
void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetBlendConstants, device_impl);
	trampoline(commandBuffer, blendConstants);

}
void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetStencilCompareMask, device_impl);
	trampoline(commandBuffer, faceMask, compareMask);

}
void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetStencilWriteMask, device_impl);
	trampoline(commandBuffer, faceMask, writeMask);

}
void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdSetStencilReference, device_impl);
	trampoline(commandBuffer, faceMask, reference);

}

void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindDescriptorSets, device_impl);
	trampoline(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

}

void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindIndexBuffer, device_impl);
	trampoline(commandBuffer, buffer, offset, indexType);

}
void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindVertexBuffers, device_impl);
	trampoline(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);

}

void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDraw, device_impl);
	trampoline(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}
void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawIndexed, device_impl);
	trampoline(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawIndirect, device_impl);
	trampoline(commandBuffer, buffer, offset, drawCount, stride);
}
void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawIndexedIndirect, device_impl);
	trampoline(commandBuffer, buffer, offset, drawCount, stride);
}
void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDispatch, device_impl);
	trampoline(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDispatchIndirect, device_impl);
	trampoline(commandBuffer, buffer, offset);
}

void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyBuffer, device_impl);

	trampoline(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}
void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyImage, device_impl);

	trampoline(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdBlitImage, device_impl);

	trampoline(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}
void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyBufferToImage, device_impl);

	trampoline(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}
void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyImageToBuffer, device_impl);

	trampoline(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdClearColorImage, device_impl);
	trampoline(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}
void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdClearDepthStencilImage, device_impl);
	trampoline(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}
void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdClearAttachments, device_impl);

	trampoline(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve *pRegions)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdResolveImage, device_impl);

	trampoline(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdPipelineBarrier, device_impl);
	trampoline(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);

}

void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdBeginQuery, device_impl);
	trampoline(commandBuffer, queryPool, query, flags);
}
void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdEndQuery, device_impl);
	trampoline(commandBuffer, queryPool, query);
}
void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdWriteTimestamp, device_impl);
	trampoline(commandBuffer, pipelineStage, queryPool, query);
}

void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdCopyQueryPoolResults, device_impl);
	trampoline(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}

void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdPushConstants, device_impl);
	trampoline(commandBuffer, layout, stageFlags, offset, size, pValues);

}

void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdBeginRenderPass, device_impl);
	trampoline(commandBuffer, pRenderPassBegin, contents);
}
void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdNextSubpass, device_impl);
	trampoline(commandBuffer, contents);
}
void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdEndRenderPass, device_impl);
	trampoline(commandBuffer);
}

void VKAPI_CALL vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdExecuteCommands, device_impl);
	trampoline(commandBuffer, commandBufferCount, pCommandBuffers);
}

void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawIndirectCount, device_impl);
	trampoline(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawIndexedIndirectCount, device_impl);
	trampoline(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, const VkSubpassBeginInfo *pSubpassBeginInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdBeginRenderPass2, device_impl);
	trampoline(commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
}
void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer commandBuffer, const VkSubpassBeginInfo *pSubpassBeginInfo, const VkSubpassEndInfo *pSubpassEndInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdNextSubpass2, device_impl);
	trampoline(commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
}
void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo *pSubpassEndInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdEndRenderPass2, device_impl);
	trampoline(commandBuffer, pSubpassEndInfo);
}

void VKAPI_CALL vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdPipelineBarrier2, device_impl);
	trampoline(commandBuffer, pDependencyInfo);

}

void VKAPI_CALL vkCmdWriteTimestamp2(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, VkQueryPool queryPool, uint32_t query)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdWriteTimestamp2, device_impl);
	trampoline(commandBuffer, stage, queryPool, query);
}

void VKAPI_CALL vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyBuffer2, device_impl);

	trampoline(commandBuffer, pCopyBufferInfo);
}
void VKAPI_CALL vkCmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyImage2, device_impl);

	trampoline(commandBuffer, pCopyImageInfo);
}
void VKAPI_CALL vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyBufferToImage2, device_impl);

	trampoline(commandBuffer, pCopyBufferToImageInfo);
}
void VKAPI_CALL vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdCopyImageToBuffer2, device_impl);

	trampoline(commandBuffer, pCopyImageToBufferInfo);
}
void VKAPI_CALL vkCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdBlitImage2, device_impl);

	trampoline(commandBuffer, pBlitImageInfo);
}
void VKAPI_CALL vkCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdResolveImage2, device_impl);

	trampoline(commandBuffer, pResolveImageInfo);
}

void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdBeginRendering, device_impl);
	trampoline(commandBuffer, pRenderingInfo);
}
void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdEndRendering, device_impl);
	trampoline(commandBuffer);
}

void VKAPI_CALL vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes, const VkDeviceSize *pStrides)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindVertexBuffers2, device_impl);
	trampoline(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes, pStrides);

}

void VKAPI_CALL vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdPushDescriptorSetKHR, device_impl);
	trampoline(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);

}

void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets, const VkDeviceSize *pSizes)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBindTransformFeedbackBuffersEXT, device_impl);
	trampoline(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets, pSizes);

}

void VKAPI_CALL vkCmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags, uint32_t index)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdBeginQueryIndexedEXT, device_impl);
	trampoline(commandBuffer, queryPool, query, flags, index);
}
void VKAPI_CALL vkCmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, uint32_t index)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdEndQueryIndexedEXT, device_impl);
	trampoline(commandBuffer, queryPool, query, index);
}

void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR *pInfos, const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
	assert(pInfos != nullptr && ppBuildRangeInfos != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));
	GET_DISPATCH_PTR_FROM(CmdBuildAccelerationStructuresKHR, device_impl);

	trampoline(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}
void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR *pInfos, const VkDeviceAddress *pIndirectDeviceAddresses, const uint32_t *pIndirectStrides, const uint32_t *const *ppMaxPrimitiveCounts)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdBuildAccelerationStructuresIndirectKHR, device_impl);
	trampoline(commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);
}

void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR *pInfo)
{
	assert(pInfo != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdCopyAccelerationStructureKHR, device_impl);
	trampoline(commandBuffer, pInfo);
}

void VKAPI_CALL vkCmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth)
{
	assert(pRaygenShaderBindingTable != nullptr && pMissShaderBindingTable != nullptr && pHitShaderBindingTable != nullptr && pCallableShaderBindingTable != nullptr);

	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdTraceRaysKHR, device_impl);
	trampoline(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}
void VKAPI_CALL vkCmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));

	GET_DISPATCH_PTR_FROM(CmdTraceRaysIndirectKHR, device_impl);
	trampoline(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
}
void VKAPI_CALL vkCmdTraceRaysIndirect2KHR(VkCommandBuffer commandBuffer, VkDeviceAddress indirectDeviceAddress)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdTraceRaysIndirect2KHR, device_impl);
	trampoline(commandBuffer, indirectDeviceAddress);
}

void VKAPI_CALL vkCmdDrawMeshTasksEXT(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawMeshTasksEXT, device_impl);
	trampoline(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawMeshTasksIndirectEXT, device_impl);
	trampoline(commandBuffer, buffer, offset, drawCount, stride);
}
void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
{
	reshade::vulkan::device_impl *const device_impl = g_vulkan_devices.at(dispatch_key_from_handle(commandBuffer));


	GET_DISPATCH_PTR_FROM(CmdDrawMeshTasksIndirectCountEXT, device_impl);
	trampoline(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
