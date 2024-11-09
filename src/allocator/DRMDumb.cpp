#include <aquamarine/allocator/DRMDumb.hpp>
#include <aquamarine/backend/Backend.hpp>
#include <aquamarine/backend/DRM.hpp>
#include <aquamarine/allocator/Swapchain.hpp>
#include "FormatUtils.hpp"
#include "Shared.hpp"
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include "../backend/drm/Renderer.hpp"

using namespace Aquamarine;
using namespace Hyprutils::Memory;
#define SP CSharedPointer
#define WP CWeakPointer

Aquamarine::CDRMDumbBuffer::CDRMDumbBuffer(const SAllocatorBufferParams& params, Hyprutils::Memory::CWeakPointer<CDRMDumbAllocator> allocator_,
                                           Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain) : allocator(allocator_) {
    attrs.format = params.format;

    if (int ret = drmModeCreateDumbBuffer(allocator->drmFD(), params.size.x, params.size.y, 32, 0, &handle, &stride, &size); ret < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("failed to create a drm_dumb buffer: {}", strerror(-ret)));
        return;
    }

    pixelSize = {(double)params.size.x, (double)params.size.y};

    attrs.size          = pixelSize;
    attrs.strides.at(0) = stride;

    uint64_t offset = 0;
    if (int ret = drmModeMapDumbBuffer(allocator->drmFD(), handle, &offset); ret < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("failed to map a drm_dumb buffer: {}", strerror(-ret)));
        return;
    }

    data = (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, allocator->drmFD(), offset);
    if (data == MAP_FAILED) {
        allocator->backend->log(AQ_LOG_ERROR, "failed to mmap a drm_dumb buffer");
        return;
    }

    // set the entire buffer so we dont get garbage
    memset(data, 0xFF, size);

    if (int ret = drmPrimeHandleToFD(allocator->drmFD(), handle, DRM_CLOEXEC, &primeFD); ret < 0) {
        allocator->backend->log(AQ_LOG_ERROR, std::format("failed to map a drm_dumb buffer: {}", strerror(-ret)));
        return;
    }

    attrs.fds.at(0) = primeFD;

    attrs.success = true;

    allocator->backend->log(AQ_LOG_DEBUG, std::format("DRM Dumb: Allocated a new buffer with primeFD {}, size {} and format {}", primeFD, attrs.size, fourccToName(attrs.format)));
}

Aquamarine::CDRMDumbBuffer::~CDRMDumbBuffer() {
    events.destroy.emit();

    TRACE(allocator->backend->log(AQ_LOG_TRACE, std::format("DRM Dumb: dropping buffer {}", primeFD)));

    if (handle == 0)
        return;

    if (data)
        munmap(data, size);

    drm_mode_destroy_dumb request = {
        .handle = handle,
    };
    drmIoctl(allocator->drmFD(), DRM_IOCTL_MODE_DESTROY_DUMB, &request);
}

eBufferCapability Aquamarine::CDRMDumbBuffer::caps() {
    return eBufferCapability::BUFFER_CAPABILITY_DATAPTR;
}

eBufferType Aquamarine::CDRMDumbBuffer::type() {
    return eBufferType::BUFFER_TYPE_DMABUF;
}

void Aquamarine::CDRMDumbBuffer::update(const Hyprutils::Math::CRegion& damage) {
    ; // nothing to do
}

bool Aquamarine::CDRMDumbBuffer::isSynchronous() {
    return true;
}

bool Aquamarine::CDRMDumbBuffer::good() {
    return attrs.success && data;
}

SDMABUFAttrs Aquamarine::CDRMDumbBuffer::dmabuf() {
    return attrs;
}

std::tuple<uint8_t*, uint32_t, size_t> Aquamarine::CDRMDumbBuffer::beginDataPtr(uint32_t flags) {
    return {data, attrs.format, size};
}

void Aquamarine::CDRMDumbBuffer::endDataPtr() {
    ; // nothing to do
}

Aquamarine::CDRMDumbAllocator::~CDRMDumbAllocator() {
    ; // nothing to do
}

SP<CDRMDumbAllocator> Aquamarine::CDRMDumbAllocator::create(int drmfd_, Hyprutils::Memory::CWeakPointer<CBackend> backend_) {
    auto a  = SP<CDRMDumbAllocator>(new CDRMDumbAllocator(drmfd_, backend_));
    a->self = a;
    return a;
}

SP<IBuffer> Aquamarine::CDRMDumbAllocator::acquire(const SAllocatorBufferParams& params, SP<CSwapchain> swapchain_) {
    auto buf = SP<IBuffer>(new CDRMDumbBuffer(params, self, swapchain_));
    if (!buf->good())
        return nullptr;
    return buf;
}

SP<CBackend> Aquamarine::CDRMDumbAllocator::getBackend() {
    return backend.lock();
}

int Aquamarine::CDRMDumbAllocator::drmFD() {
    return drmfd;
}

eAllocatorType Aquamarine::CDRMDumbAllocator::type() {
    return eAllocatorType::AQ_ALLOCATOR_TYPE_DRM_DUMB;
}

Aquamarine::CDRMDumbAllocator::CDRMDumbAllocator(int fd_, Hyprutils::Memory::CWeakPointer<CBackend> backend_) : drmfd(fd_), backend(backend_) {
    ; // nothing to do
}
