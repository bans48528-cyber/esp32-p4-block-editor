#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <string.h>
#include "editor.h"
static uint32_t framebuffer[800*480],buffer[800*480];
static lv_point_t pointer;
static bool pressed;
static HWND window;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *data){
    uint32_t *p=(uint32_t*)data;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)framebuffer[y*800+x]=*p++;
    lv_display_flush_ready(d);InvalidateRect(window,NULL,FALSE);
}
static void input_read(lv_indev_t *i,lv_indev_data_t *d){(void)i;d->point=pointer;d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static uint32_t tick(void){return GetTickCount();}
static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_LBUTTONDOWN:SetCapture(w);pressed=true;pointer.x=GET_X_LPARAM(lp);pointer.y=GET_Y_LPARAM(lp);return 0;
    case WM_MOUSEMOVE:pointer.x=GET_X_LPARAM(lp);pointer.y=GET_Y_LPARAM(lp);return 0;
    case WM_LBUTTONUP:pressed=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:case WM_KILLFOCUS:pressed=false;return 0;
    case WM_TIMER:lv_timer_handler();return 0;
    case WM_PAINT:{
        PAINTSTRUCT paint;HDC dc=BeginPaint(w,&paint);BITMAPINFO info={0};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=800;info.bmiHeader.biHeight=-480;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        SetDIBitsToDevice(dc,0,0,800,480,0,0,0,480,framebuffer,&info,DIB_RGB_COLORS);
        EndPaint(w,&paint);return 0;
    }
    case WM_DESTROY:KillTimer(w,1);PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,m,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show){
    (void)previous;(void)command;SetProcessDPIAware();
    WNDCLASSW cls={0};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"BoardEditorPrototype";cls.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&cls);
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT r={0,0,800,480};AdjustWindowRect(&r,style,FALSE);
    window=CreateWindowW(cls.lpszClassName,strstr(command,"--web-style")?L"网页轮廓样式预览 · 800×480":strstr(command,"--continuous")?L"连续接缝预览 · 800×480":strstr(command,"--loop-edge")?L"循环边缘修复预览 · 800×480":strstr(command,"--uniform")?L"统一轮廓预览 · 800×480":strstr(command,"--seams")?L"积木贴合细线预览 · 800×480":strstr(command,"--outline")?L"积木描边预览 · 800×480":strstr(command,"--projection")?L"占位投影预览 · 800×480":strstr(command,"--connectors")?L"连接口修复预览 · 800×480":L"LVGL 编程板原型 · 固定 800×480",style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);
    if(!window)return 1;
    lv_init();lv_tick_set_cb(tick);lv_display_t *display=lv_display_create(800,480);lv_display_set_flush_cb(display,flush);
    lv_display_set_buffers(display,buffer,NULL,sizeof buffer,LV_DISPLAY_RENDER_MODE_FULL);
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,input_read);
    editor_create();ShowWindow(window,show);SetTimer(window,1,16,NULL);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}lv_deinit();return 0;
}
