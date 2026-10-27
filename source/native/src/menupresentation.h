#pragma once
namespace menupresentation {
enum class Route { Mask, Menu, Loading };
inline Route Choose(bool menuReady,bool explicitMenu,bool pendingView,bool viewportReady,
                    bool engineMatches,bool travelling){
    if(menuReady&&(explicitMenu||!pendingView)&&(!viewportReady||engineMatches))return Route::Menu;
    // Level travel cannot safely call UPlayer::Exec until its actor returns.
    // Show that raw loading frame in an independent quad while keeping the
    // existing D3D device. A pending player view or pause must stay masked.
    // Once travel is established, reattaching the player does not hide the
    // quad while a queued resize is still waiting on the game thread.
    if(!menuReady&&!explicitMenu&&!pendingView&&travelling)return Route::Loading;
    return Route::Mask;
}
}
