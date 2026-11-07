// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <boost/container/deque.hpp>

#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "video_core/renderer_vulkan/vk_display_pacer.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Core::Frontend {
class EmuWindow;
} // namespace Core::Frontend

namespace Vulkan {

class Device;
class Scheduler;
class Swapchain;

struct Frame {
    u32 width;
    u32 height;
    u32 index;
    vk::Image image;
    vk::ImageView image_view;
    vk::ImageView storage_view;
    vk::Framebuffer framebuffer;
    vk::CommandBuffer cmdbuf;
    vk::Semaphore render_ready;
    vk::Fence present_done;
    bool storage_capable{};
    /// Vblanks this frame follows the previous one by in game time (0: present when ready).
    u32 display_vblanks{};
};

class PresentManager {
public:
    PresentManager(const vk::Instance& instance,
                   Core::Frontend::EmuWindow& render_window,
                   const Device& device,
                   MemoryAllocator& memory_allocator,
                   Scheduler& scheduler,
                   Swapchain& swapchain,
                   vk::SurfaceKHR& surface);
    ~PresentManager();

    /// Returns the last used presentation frame
    Frame* GetRenderFrame();

    /// Pushes a frame for presentation
    void Present(Frame* frame);

    /// Recreates the present frame to match the provided parameters
    void RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat image_view_format,
                       VkRenderPass rd, bool storage);

    [[nodiscard]] bool NeedsStorage(const Frame* frame, bool required) const;

    /// Waits for the present thread to finish presenting all queued frames.
    void WaitPresent();

    /// How many additional frames can be queued without stalling the render thread
    [[nodiscard]] size_t MaxExtraFrames() const;

    [[nodiscard]] std::size_t SwapchainImageCount() const { return swapchain_image_count; }
    [[nodiscard]] VkFormat SwapchainImageFormat() const { return swapchain_image_format; }

private:
    void PresentThread(std::stop_token token);

    void CopyToSwapchain(Frame* frame);

    void CopyToSwapchainImpl(Frame* frame);

    void RecreateSwapchain(Frame* frame);

    void DiscardFrame(Frame* frame);

    /// Holds a frame until its game-time step since the previous one has passed (bounded).
    void PaceToGameTime(const Frame* frame);

    /// Mario Kart World: queues the frame on the display's refresh grid when the frame rate
    /// limiter is set to a fixed rate the display can show evenly (see DisplayPacer).
    void PaceToDisplay(bool backlog);

    void SetImageCount();

private:
    const vk::Instance& instance;
    Core::Frontend::EmuWindow& render_window;
    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    Swapchain& swapchain;
    vk::SurfaceKHR& surface;
    vk::CommandPool cmdpool;
    std::vector<Frame> frames;
    boost::container::deque<Frame*> present_queue;
    boost::container::deque<Frame*> free_queue;
    std::condition_variable_any frame_cv;
    std::condition_variable free_cv;
    std::mutex swapchain_mutex;
    std::mutex queue_mutex;
    std::mutex free_mutex;
    std::jthread present_thread;
    bool blit_supported;
    bool storage_supported;
    bool use_present_thread;
    std::atomic<std::size_t> image_count{};
    std::atomic<std::size_t> swapchain_image_count{};
    std::atomic<VkFormat> swapchain_image_format{};
    std::chrono::steady_clock::time_point last_display{};
    bool have_last_display{};
    DisplayPacer display_pacer;
};

} // namespace Vulkan
