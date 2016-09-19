#pragma once

#if defined(__DAVAENGINE_COREV2__)

#include "Base/BaseTypes.h"

#if defined(__DAVAENGINE_QT__)
// TODO: plarform defines
#elif defined(__DAVAENGINE_MACOS__)

#include "Functional/Function.h"

#include "Engine/Private/EnginePrivateFwd.h"
#include "Engine/Private/WindowBackendBase.h"

namespace rhi
{
struct InitParam;
}

namespace DAVA
{
namespace Private
{
class WindowBackend final : public WindowBackendBase
{
public:
    WindowBackend(EngineBackend* engineBackend, Window* window);
    ~WindowBackend();

    bool Create(float32 width, float32 height);
    void Resize(float32 width, float32 height);
    void Close();
    void Detach();

    void* GetHandle() const;
    WindowNativeService* GetNativeService() const;

    bool IsWindowReadyForRender() const;
    void InitCustomRenderParams(rhi::InitParam& params);

    void TriggerPlatformEvents();
    void ProcessPlatformEvents();

private:
    void UIEventHandler(const UIDispatcherEvent& e);
    void WindowWillClose();

private:
    EngineBackend* engineBackend = nullptr;
    std::unique_ptr<WindowNativeBridge> bridge;
    std::unique_ptr<WindowNativeService> nativeService;

    bool isMinimized = false;
    bool closeRequestByApp = false;
    size_t hideUnhideSignalId = 0;

    // Friends
    friend class PlatformCore;
    friend struct WindowNativeBridge;
};

inline WindowNativeService* WindowBackend::GetNativeService() const
{
    return nativeService.get();
}

inline void WindowBackend::InitCustomRenderParams(rhi::InitParam& /*params*/)
{
    // No custom render params
}

} // namespace Private
} // namespace DAVA

#endif // __DAVAENGINE_MACOS__
#endif // __DAVAENGINE_COREV2__
