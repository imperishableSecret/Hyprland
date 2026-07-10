#include "GLFramebuffer.hpp"
#include "PixelPackLayout.hpp"
#include "../OpenGL.hpp"
#include "../Renderer.hpp"
#include "macros.hpp"
#include "../Framebuffer.hpp"
#include <hyprgraphics/egl/Egl.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

using namespace Hyprgraphics::Egl;
using namespace Hyprutils::Utils;
using namespace Render::GL;

CGLFramebuffer::CGLFramebuffer() : IFramebuffer() {}
CGLFramebuffer::CGLFramebuffer(const std::string& name) : IFramebuffer(name) {}

bool CGLFramebuffer::internalAlloc(int w, int h, uint32_t drmFormat) {
    g_pHyprOpenGL->makeEGLCurrent();

    if (!m_tex) {
        m_tex = g_pHyprRenderer->createTexture();
        m_tex->allocate({w, h}, drmFormat);
        m_tex->bind();
        m_tex->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_tex->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    if (!m_fbAllocated) {
        glGenFramebuffers(1, &m_fb);
        m_fbAllocated = true;
    }

    const auto format = getPixelFormatFromDRM(drmFormat);
    m_tex->bind();
    glTexImage2D(GL_TEXTURE_2D, 0, format->glInternalFormat ? format->glInternalFormat : format->glFormat, w, h, 0, format->glFormat, format->glType, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex->m_texID, 0);

    if (m_mirrorTex) {
        const auto format = getPixelFormatFromDRM(m_mirrorTex->m_drmFormat);
        m_mirrorTex->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, format->glInternalFormat ? format->glInternalFormat : format->glFormat, w, h, 0, format->glFormat, format->glType, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_mirrorTex->m_texID, 0);
    } else
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);

    if (m_stencilTex && m_stencilTex->ok()) {
        m_stencilTex->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_stencilTex->m_texID, 0);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    RASSERT((status == GL_FRAMEBUFFER_COMPLETE), "Framebuffer incomplete, couldn't create! (FB status: {}, GL Error: 0x{:x})", status, sc<int>(glGetError()));

    if (m_stencilTex && m_stencilTex->ok())
        m_stencilTex->unbind();

    Log::logger->log(Log::DEBUG, "Framebuffer \"{}\" created, status {}", m_name, status);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

void CGLFramebuffer::addStencil(SP<ITexture> tex) {
    if (m_stencilTex == tex)
        return;

    m_stencilTex = tex;

    if (!m_fbAllocated)
        return;

    bind();
    if (m_stencilTex && m_stencilTex->ok()) {
        m_stencilTex->bind();
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_stencilTex->m_texID, 0);
        m_stencilTex->unbind();
    } else
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

    const auto STATUS = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    RASSERT(STATUS == GL_FRAMEBUFFER_COMPLETE, "Framebuffer incomplete after changing stencil attachment (status: {})", STATUS);
}

void CGLFramebuffer::bind() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fb);

    if (g_pHyprOpenGL) {
        const auto& size = g_pHyprRenderer->m_renderData.pMonitor ? g_pHyprRenderer->m_renderData.pMonitor->m_pixelSize : m_size;
        g_pHyprOpenGL->setViewport(0, 0, size.x, size.y);
    } else
        glViewport(0, 0, m_size.x, m_size.y);
}

void CGLFramebuffer::unbind() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void CGLFramebuffer::release() {
    if (m_fbAllocated) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        if (m_mirrorTex)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glDeleteFramebuffers(1, &m_fb);
        m_fbAllocated = false;
        m_fb          = 0;
    }

    if (m_tex)
        m_tex.reset();

    m_size = Vector2D();
}

bool CGLFramebuffer::readPixels(CHLBufferReference buffer, uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height) {
    auto shm = buffer->shm();
    if (!shm.success) {
        LOGM(Log::ERR, "Can't copy: buffer is not shm");
        return false;
    }

    auto [pixelData, fmt, bufLen] = buffer->beginDataPtr(0); // no need for end, cuz it's shm
    if (!pixelData) {
        LOGM(Log::ERR, "Can't copy: failed to get shm data pointer");
        return false;
    }

    const auto PFORMAT = getPixelFormatFromDRM(shm.format);
    if (!PFORMAT) {
        LOGM(Log::ERR, "Can't copy: failed to find a pixel format");
        return false;
    }

    const auto fbWidth    = sc<uint32_t>(m_size.x);
    const auto fbHeight   = sc<uint32_t>(m_size.y);
    const auto readWidth  = width > 0 ? width : fbWidth;
    const auto readHeight = height > 0 ? height : fbHeight;

    if (readWidth == 0 || readHeight == 0 || shm.stride <= 0) {
        LOGM(Log::ERR, "Can't copy: invalid shm read dimensions");
        return false;
    }

    if (offsetX > fbWidth || offsetY > fbHeight || readWidth > fbWidth - offsetX || readHeight > fbHeight - offsetY) {
        LOGM(Log::ERR, "Can't copy: read rect exceeds framebuffer");
        return false;
    }

    const auto shmWidth  = sc<uint32_t>(shm.size.x);
    const auto shmHeight = sc<uint32_t>(shm.size.y);
    if (offsetX > shmWidth || offsetY > shmHeight || readWidth > shmWidth - offsetX || readHeight > shmHeight - offsetY) {
        LOGM(Log::ERR, "Can't copy: read rect exceeds shm buffer");
        return false;
    }

    const auto strideBytes = sc<size_t>(shm.stride);
    const auto rowOffset   = sc<size_t>(minStride(PFORMAT, offsetX));
    const auto rowBytes    = sc<size_t>(minStride(PFORMAT, readWidth));

    if (rowBytes == 0) {
        LOGM(Log::ERR, "Can't copy: invalid shm row size");
        return false;
    }

    const auto layout = calculatePixelPackLayout(bufLen, strideBytes, rowOffset, rowBytes, PFORMAT->bytesPerBlock, offsetX, offsetY, readWidth, readHeight);
    if (!layout) {
        LOGM(Log::ERR, "Can't copy: invalid shm buffer layout");
        return false;
    }

    g_pHyprOpenGL->makeEGLCurrent();

    GLint previousReadFB = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFB);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, getFBID());
    CScopeGuard restoreReadFB([previousReadFB] { glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFB); });

    int         glFormat = PFORMAT->glFormat;

    static auto stripSwizzleAlpha = [](std::array<GLint, 4> arr) {
        arr[3] = GL_ONE;
        return arr;
    };

    if (PFORMAT->swizzle.has_value()) {
        if (stripSwizzleAlpha(*PFORMAT->swizzle) == stripSwizzleAlpha(SWIZZLE_RGBA))
            glFormat = GL_RGBA;
        else if (stripSwizzleAlpha(*PFORMAT->swizzle) == stripSwizzleAlpha(SWIZZLE_BGRA))
            glFormat = GL_BGRA_EXT;
        else {
            LOGM(Log::ERR, "Copied frame via shm might be broken or color flipped");
            glFormat = GL_RGBA;
        }
    } else if (glFormat == GL_RGBA)
        glFormat = GL_BGRA_EXT;

    GLint previousAlignment = 0, previousRowLength = 0, previousSkipPixels = 0, previousSkipRows = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &previousRowLength);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &previousSkipPixels);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &previousSkipRows);

    CScopeGuard restorePixelStore([=] {
        glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, previousRowLength);
        glPixelStorei(GL_PACK_SKIP_PIXELS, previousSkipPixels);
        glPixelStorei(GL_PACK_SKIP_ROWS, previousSkipRows);
    });

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // This could be made asynchronous with a pixel buffer object, but clients
    // with latency-sensitive capture should prefer DMA-BUF.
    if (layout->direct) {
        glPixelStorei(GL_PACK_ROW_LENGTH, layout->rowLength);
        glPixelStorei(GL_PACK_SKIP_PIXELS, layout->skipPixels);
        glPixelStorei(GL_PACK_SKIP_ROWS, layout->skipRows);
        glReadPixels(offsetX, offsetY, readWidth, readHeight, glFormat, PFORMAT->glType, pixelData);
    } else {
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);

        std::vector<uint8_t> scratch(layout->scratchBytes);
        glReadPixels(offsetX, offsetY, readWidth, readHeight, glFormat, PFORMAT->glType, scratch.data());

        for (uint32_t i = 0; i < readHeight; ++i) {
            const auto SOURCE_OFFSET      = sc<size_t>(i) * layout->rowBytes;
            const auto DESTINATION_OFFSET = layout->destinationOffset + sc<size_t>(i) * strideBytes;
            std::memcpy(pixelData + DESTINATION_OFFSET, scratch.data() + SOURCE_OFFSET, layout->rowBytes);
        }
    }
    return true;
}

CGLFramebuffer::~CGLFramebuffer() {
    release();
}

GLuint CGLFramebuffer::getFBID() {
    return m_fbAllocated ? m_fb : 0;
}

void CGLFramebuffer::invalidate(const std::vector<GLenum>& attachments) {
    if (!m_fbAllocated)
        return;

    glInvalidateFramebuffer(GL_FRAMEBUFFER, attachments.size(), attachments.data());
    if (std::ranges::contains(attachments, GL_COLOR_ATTACHMENT0))
        m_cleared = false;
}

void CGLFramebuffer::clearAfterInvalidation() {
    if (m_cleared)
        return;

    m_cleared = true;
    glClearColor(0, 0, 0, 0);
    g_pHyprOpenGL->scissor(nullptr);
    glClear(GL_COLOR_BUFFER_BIT);
}
