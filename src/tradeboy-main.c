#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

// 简单的即时模式UI组件
typedef struct {
    SDL_Rect rect;
    const char* text;
    SDL_Color bg_color;
    SDL_Color text_color;
    int hovered;
    int pressed;
} UIButton;

typedef struct {
    SDL_Rect rect;
    const char* title;
    const char* items[10];
    int item_count;
    int selected_index;
    SDL_Color bg_color;
    SDL_Color border_color;
    SDL_Color text_color;
    SDL_Color selected_color;
} UIListBox;

typedef struct {
    SDL_Rect rect;
    const char* title;
    const char* content;
    SDL_Color bg_color;
    SDL_Color border_color;
    SDL_Color text_color;
} UIDialog;

// 应用上下文结构
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    int running;
    int width;
    int height;
    time_t last_input_time;
    char last_key_info[256];
    char input_info[256];
    int frame_count;
    
    // TradeBoy特定功能
    int selected_item;
    int menu_items_count;
    char menu_items[10][64];
    char item_prices[10][32];
    char item_descriptions[10][128];
    
    // 帧缓冲模式
    int framebuffer_mode;
    int fb_fd;
} tradeboy_context_t;

// 日志文件
FILE* log_file = NULL;

// 日志函数
void log_message(const char* message) {
    if (log_file) {
        time_t now = time(NULL);
        char* time_str = ctime(&now);
        time_str[strlen(time_str) - 1] = '\0'; // 移除换行符
        fprintf(log_file, "[%s] %s\n", time_str, message);
        fflush(log_file);
    }
    printf("%s\n", message);
}

// 打开日志文件
void open_log_file() {
    log_file = fopen("/tmp/tradeboy.log", "a");
    if (log_file) {
        fprintf(log_file, "\n=== TradeBoy Application Started ===\n");
        fflush(log_file);
    }
}

// 关闭日志文件
void close_log_file() {
    if (log_file) {
        fprintf(log_file, "=== TradeBoy Application Ended ===\n\n");
        fclose(log_file);
        log_file = NULL;
    }
}

// 初始化SDL2
int init_sdl2(tradeboy_context_t* app) {
    // 设置UTF-8编码
    setlocale(LC_ALL, "en_US.UTF-8");
    
    // 强制使用OpenGL ES (Anbernic设备使用GLESv2)
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    #ifdef __linux__
    // SDL_HINT_VIDEO_DRIVER 可能在旧版SDL2中不存在
    // SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "gles2");
    #endif
    
    // 设置OpenGL ES属性
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // 初始化SDL2 (包含视频和手柄子系统)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        char error_msg[128];
        sprintf(error_msg, "SDL2 initialization failed: %s", SDL_GetError());
        log_message(error_msg);
        return 0;
    }
    
    // 初始化手柄子系统
    SDL_JoystickEventState(SDL_ENABLE);
    
    // 检测手柄数量
    int num_joysticks = SDL_NumJoysticks();
    char joystick_info[128];
    sprintf(joystick_info, "Detected %d joystick(s)", num_joysticks);
    log_message(joystick_info);
    
    // 打开所有手柄
    for (int i = 0; i < num_joysticks; i++) {
        SDL_Joystick* joystick = SDL_JoystickOpen(i);
        if (joystick) {
            char joy_name[128];
            sprintf(joy_name, "Opened joystick %d: %s", i, SDL_JoystickName(joystick));
            log_message(joy_name);
        }
    }
    
    // 初始化TTF
    if (TTF_Init() < 0) {
        char error_msg[128];
        sprintf(error_msg, "TTF initialization failed: %s", TTF_GetError());
        log_message(error_msg);
        return 0;
    }
    
    // 创建窗口
    #ifdef __linux__
        // 检查是否为帧缓冲模式
        char debug_msg[256];
        sprintf(debug_msg, "Checking framebuffer: access(/dev/fb0) = %d", access("/dev/fb0", F_OK));
        log_message(debug_msg);
        
        if (access("/dev/fb0", F_OK) == 0) {
            log_message("Framebuffer device detected, using framebuffer mode");
            app->framebuffer_mode = 1;
            
            // 尝试从帧缓冲创建窗口 (使用OpenGL ES)
            int fb_fd = open("/dev/fb0", O_RDWR);
            if (fb_fd >= 0) {
                struct fb_var_screeninfo vinfo;
                if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) >= 0) {
                    char fb_info[256];
                    sprintf(fb_info, "Framebuffer: %dx%d, bpp: %d", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
                    log_message(fb_info);
                    log_message("Creating SDL2 window with OpenGL ES");
                    app->fb_fd = fb_fd;
                    app->width = vinfo.xres;
                    app->height = vinfo.yres;
                    
                    // 使用OpenGL ES创建窗口
                    app->window = SDL_CreateWindow("TradeBoy - GLES2", 
                                              SDL_WINDOWPOS_UNDEFINED, 
                                              SDL_WINDOWPOS_UNDEFINED, 
                                              app->width, app->height, 
                                              SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
                    if (app->window) {
                        // 创建OpenGL ES上下文
                        SDL_GLContext gl_context = SDL_GL_CreateContext(app->window);
                        if (gl_context) {
                            log_message("OpenGL ES context created successfully");
                            SDL_GL_MakeCurrent(app->window, gl_context);
                        } else {
                            log_message("Failed to create OpenGL ES context");
                        }
                    } else {
                        char fb_error[256];
                        sprintf(fb_error, "GLES window creation failed: %s", SDL_GetError());
                        log_message(fb_error);
                    }
                } else {
                    log_message("Failed to get framebuffer info");
                    close(fb_fd);
                }
            } else {
                log_message("Failed to open framebuffer device");
            }
        } else {
            log_message("Framebuffer device NOT detected");
        }
    #else
        log_message("Not Linux, skipping framebuffer check");
    #endif
    
    // 如果不是帧缓冲模式，创建普通窗口
    if (!app->window) {
        log_message("Creating normal SDL2 window with OpenGL ES");
        app->framebuffer_mode = 0;
        app->window = SDL_CreateWindow("TradeBoy - RG34XX Trading App", 
                                      SDL_WINDOWPOS_CENTERED, 
                                      SDL_WINDOWPOS_CENTERED, 
                                      app->width, app->height, 
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    }
    
    if (!app->window) {
        char error_msg[128];
        sprintf(error_msg, "Window creation failed: %s", SDL_GetError());
        log_message(error_msg);
        return 0;
    }
    
    // 创建渲染器
    app->renderer = SDL_CreateRenderer(app->window, -1, 
                                    SDL_RENDERER_ACCELERATED | 
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) {
        char error_msg[128];
        sprintf(error_msg, "Renderer creation failed: %s", SDL_GetError());
        log_message(error_msg);
        
        // 尝试软件渲染作为备选
        log_message("Trying software renderer as fallback");
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
        if (!app->renderer) {
            sprintf(error_msg, "Software renderer creation failed: %s", SDL_GetError());
            log_message(error_msg);
            return 0;
        } else {
            log_message("Software renderer created successfully");
        }
    } else {
        // 检查渲染器信息
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(app->renderer, &info) == 0) {
            char renderer_info[256];
            sprintf(renderer_info, "Renderer: %s, Flags: 0x%x", info.name, info.flags);
            log_message(renderer_info);
        }
    }
    
    // 加载字体
    const char* font_paths[] = {
        "./NotoSansCJK-Regular.ttc",
        "/mnt/mmc/Roms/APPS/NotoSansCJK-Regular.ttc",
        "NotoSansCJK-Regular.ttc",
        NULL
    };
    
    for (int i = 0; font_paths[i]; i++) {
        app->font = TTF_OpenFont(font_paths[i], 16);
        if (app->font) {
            char font_msg[128];
            sprintf(font_msg, "Font loaded successfully from: %s", font_paths[i]);
            log_message(font_msg);
            break;
        }
    }
    
    if (!app->font) {
        log_message("Font loading failed, using default font");
        app->font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16);
    }
    
    if (!app->font) {
        log_message("No font available");
        return 0;
    }
    
    log_message("SDL2 initialized successfully");
    return 1;
}

// 初始化TradeBoy数据
void init_tradeboy_data(tradeboy_context_t* app) {
    // 初始化菜单项
    app->menu_items_count = 5;
    strcpy(app->menu_items[0], "🎮 游戏卡带");
    strcpy(app->item_prices[0], "¥89.99");
    strcpy(app->item_descriptions[0], "经典GBA游戏卡带，支持多平台");
    
    strcpy(app->menu_items[1], "🎯 手柄配件");
    strcpy(app->item_prices[1], "¥45.50");
    strcpy(app->item_descriptions[1], "高品质游戏手柄，精准控制");
    
    strcpy(app->menu_items[2], "📱 手机支架");
    strcpy(app->item_prices[2], "¥28.00");
    strcpy(app->item_descriptions[2], "便携式手机支架，多角度调节");
    
    strcpy(app->menu_items[3], "🔋 充电宝");
    strcpy(app->item_prices[3], "¥128.00");
    strcpy(app->item_descriptions[3], "大容量移动电源，快速充电");
    
    strcpy(app->menu_items[4], "🎧 蓝牙耳机");
    strcpy(app->item_prices[4], "¥199.99");
    strcpy(app->item_descriptions[4], "无线蓝牙耳机，降噪功能");
    
    app->selected_item = 0;
    
    log_message("TradeBoy data initialized");
}

// 处理键盘事件
void handle_keyboard_event(tradeboy_context_t* app, SDL_KeyboardEvent* event) {
    if (event->type == SDL_KEYDOWN) {
        app->last_input_time = time(NULL);
        
        switch (event->keysym.sym) {
            case SDLK_UP:
                if (app->selected_item > 0) {
                    app->selected_item--;
                }
                sprintf(app->last_key_info, "Key: UP | Selected: %d", app->selected_item);
                sprintf(app->input_info, "选中项目: %s", app->menu_items[app->selected_item]);
                break;
                
            case SDLK_DOWN:
                if (app->selected_item < app->menu_items_count - 1) {
                    app->selected_item++;
                }
                sprintf(app->last_key_info, "Key: DOWN | Selected: %d", app->selected_item);
                sprintf(app->input_info, "选中项目: %s", app->menu_items[app->selected_item]);
                break;
                
            case SDLK_RETURN:
            case SDLK_SPACE:
                sprintf(app->last_key_info, "Key: ENTER | Item: %s", app->menu_items[app->selected_item]);
                sprintf(app->input_info, "购买: %s - %s", app->menu_items[app->selected_item], app->item_prices[app->selected_item]);
                break;
                
            case SDLK_ESCAPE:
            case SDLK_q:
                log_message("Quit key pressed");
                app->running = 0;
                break;
        }
        
        log_message(app->last_key_info);
    }
}

// 处理手柄事件
void handle_joystick_event(tradeboy_context_t* app, SDL_JoyButtonEvent* event) {
    char button_name[16];
    switch (event->button) {
        case 0: strcpy(button_name, "A"); break;
        case 1: strcpy(button_name, "B"); break;
        case 2: strcpy(button_name, "Y"); break;
        case 3: strcpy(button_name, "X"); break;
        case 4: strcpy(button_name, "L1"); break;
        case 5: strcpy(button_name, "R1"); break;
        case 6: strcpy(button_name, "SELECT"); break;
        case 7: strcpy(button_name, "START"); break;
        case 8: strcpy(button_name, "M"); break;
        case 9: strcpy(button_name, "L2"); break;
        case 10: strcpy(button_name, "R2"); break;
        case 13: strcpy(button_name, "VOL-"); break;
        case 14: strcpy(button_name, "VOL+"); break;
        default: 
            sprintf(button_name, "Btn%d", (int)event->button);
            break;
    }
    
    const char* state = (event->state == SDL_PRESSED) ? "Pressed" : "Released";
    
    if (event->state == SDL_PRESSED) {
        app->last_input_time = time(NULL);
        
        // 处理TradeBoy特定按键
        if (event->button == 0 || event->button == 1) { // A或B键
            if (app->selected_item > 0) {
                app->selected_item--;
            }
        } else if (event->button == 2 || event->button == 3) { // X或Y键
            if (app->selected_item < app->menu_items_count - 1) {
                app->selected_item++;
            }
        } else if (event->button == 7) { // START键
            sprintf(app->input_info, "购买: %s - %s", app->menu_items[app->selected_item], app->item_prices[app->selected_item]);
        }
        
        sprintf(app->last_key_info, "Joystick: %s %s | Selected: %d", button_name, state, app->selected_item);
        sprintf(app->input_info, "选中项目: %s", app->menu_items[app->selected_item]);
        log_message(app->last_key_info);
    }
}

// 处理摇杆事件
void handle_joystick_axis(tradeboy_context_t* app, SDL_JoyAxisEvent* event) {
    if (event->axis == 1) { // Y轴
        if (event->value < -8000) {
            app->last_input_time = time(NULL);
            if (app->selected_item > 0) {
                app->selected_item--;
            }
            sprintf(app->input_info, "选中项目: %s", app->menu_items[app->selected_item]);
            log_message("Joystick: UP");
        } else if (event->value > 8000) {
            app->last_input_time = time(NULL);
            if (app->selected_item < app->menu_items_count - 1) {
                app->selected_item++;
            }
            sprintf(app->input_info, "选中项目: %s", app->menu_items[app->selected_item]);
            log_message("Joystick: DOWN");
        }
    }
}

// 处理事件
void handle_events(tradeboy_context_t* app) {
    SDL_Event event;
    
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                log_message("Quit event received");
                app->running = 0;
                break;
                
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                handle_keyboard_event(app, &event.key);
                break;
                
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                handle_joystick_event(app, &event.jbutton);
                break;
                
            case SDL_JOYAXISMOTION:
                handle_joystick_axis(app, &event.jaxis);
                break;
        }
    }
}

// 渲染按钮
void render_button(SDL_Renderer* renderer, TTF_Font* font, UIButton* button) {
    // 绘制按钮背景
    SDL_SetRenderDrawColor(renderer, button->bg_color.r, button->bg_color.g, button->bg_color.b, button->bg_color.a);
    SDL_RenderFillRect(renderer, &button->rect);
    
    // 绘制边框
    SDL_SetRenderDrawColor(renderer, button->hovered ? 255 : 200, button->hovered ? 255 : 200, button->hovered ? 255 : 200, 255);
    SDL_RenderDrawRect(renderer, &button->rect);
    
    // 渲染文字
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, button->text, button->text_color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect text_rect = {
                button->rect.x + (button->rect.w - surface->w) / 2,
                button->rect.y + (button->rect.h - surface->h) / 2,
                surface->w, surface->h
            };
            SDL_RenderCopy(renderer, texture, NULL, &text_rect);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }
}

// 渲染列表框
void render_listbox(SDL_Renderer* renderer, TTF_Font* font, UIListBox* listbox) {
    // 绘制背景
    SDL_SetRenderDrawColor(renderer, listbox->bg_color.r, listbox->bg_color.g, listbox->bg_color.b, listbox->bg_color.a);
    SDL_RenderFillRect(renderer, &listbox->rect);
    
    // 绘制边框
    SDL_SetRenderDrawColor(renderer, listbox->border_color.r, listbox->border_color.g, listbox->border_color.b, listbox->border_color.a);
    SDL_RenderDrawRect(renderer, &listbox->rect);
    
    // 渲染标题
    SDL_Surface* title_surface = TTF_RenderUTF8_Blended(font, listbox->title, listbox->text_color);
    if (title_surface) {
        SDL_Texture* title_texture = SDL_CreateTextureFromSurface(renderer, title_surface);
        if (title_texture) {
            SDL_Rect title_rect = {
                listbox->rect.x + 10,
                listbox->rect.y + 10,
                title_surface->w, title_surface->h
            };
            SDL_RenderCopy(renderer, title_texture, NULL, &title_rect);
            SDL_DestroyTexture(title_texture);
        }
        SDL_FreeSurface(title_surface);
    }
    
    // 渲染列表项
    int item_height = 30;
    int start_y = listbox->rect.y + 40;
    
    for (int i = 0; i < listbox->item_count; i++) {
        SDL_Color item_color = (i == listbox->selected_index) ? listbox->selected_color : listbox->text_color;
        
        SDL_Surface* item_surface = TTF_RenderUTF8_Blended(font, listbox->items[i], item_color);
        if (item_surface) {
            SDL_Texture* item_texture = SDL_CreateTextureFromSurface(renderer, item_surface);
            if (item_texture) {
                SDL_Rect item_rect = {
                    listbox->rect.x + 10,
                    start_y + i * item_height,
                    item_surface->w, item_surface->h
                };
                SDL_RenderCopy(renderer, item_texture, NULL, &item_rect);
                SDL_DestroyTexture(item_texture);
            }
            SDL_FreeSurface(item_surface);
        }
    }
}

// 渲染对话框
void render_dialog(SDL_Renderer* renderer, TTF_Font* font, UIDialog* dialog) {
    // 绘制半透明背景
    SDL_SetRenderDrawColor(renderer, dialog->bg_color.r, dialog->bg_color.g, dialog->bg_color.b, dialog->bg_color.a);
    SDL_RenderFillRect(renderer, &dialog->rect);
    
    // 绘制边框
    SDL_SetRenderDrawColor(renderer, dialog->border_color.r, dialog->border_color.g, dialog->border_color.b, dialog->border_color.a);
    SDL_RenderDrawRect(renderer, &dialog->rect);
    
    // 渲染标题
    SDL_Surface* title_surface = TTF_RenderUTF8_Blended(font, dialog->title, dialog->text_color);
    if (title_surface) {
        SDL_Texture* title_texture = SDL_CreateTextureFromSurface(renderer, title_surface);
        if (title_texture) {
            SDL_Rect title_rect = {
                dialog->rect.x + (dialog->rect.w - title_surface->w) / 2,
                dialog->rect.y + 20,
                title_surface->w, title_surface->h
            };
            SDL_RenderCopy(renderer, title_texture, NULL, &title_rect);
            SDL_DestroyTexture(title_texture);
        }
        SDL_FreeSurface(title_surface);
    }
    
    // 渲染内容
    SDL_Surface* content_surface = TTF_RenderUTF8_Blended(font, dialog->content, dialog->text_color);
    if (content_surface) {
        SDL_Texture* content_texture = SDL_CreateTextureFromSurface(renderer, content_surface);
        if (content_texture) {
            SDL_Rect content_rect = {
                dialog->rect.x + (dialog->rect.w - content_surface->w) / 2,
                dialog->rect.y + 60,
                content_surface->w, content_surface->h
            };
            SDL_RenderCopy(renderer, content_texture, NULL, &content_rect);
            SDL_DestroyTexture(content_texture);
        }
        SDL_FreeSurface(content_surface);
    }
}

// 渲染界面
void render_ui(tradeboy_context_t* app) {
    // 清屏
    SDL_SetRenderDrawColor(app->renderer, 20, 20, 40, 255);
    SDL_RenderClear(app->renderer);
    
    // 创建UI组件
    UIListBox product_list = {
        .rect = {40, 80, 400, 300},
        .title = "🛒 商品列表",
        .items = {"🎮 游戏卡带", "🎯 手柄配件", "📱 手机支架", "🔋 充电宝", "🎧 蓝牙耳机"},
        .item_count = 5,
        .selected_index = app->selected_item,
        .bg_color = {40, 40, 60, 200},
        .border_color = {100, 100, 120, 255},
        .text_color = {200, 200, 200, 255},
        .selected_color = {255, 255, 0, 255}
    };
    
    // 商品详情对话框
    UIDialog product_info = {
        .rect = {460, 80, 240, 200},
        .title = "商品详情",
        .content = app->item_descriptions[app->selected_item],
        .bg_color = {30, 30, 50, 220},
        .border_color = {80, 80, 100, 255},
        .text_color = {180, 180, 180, 255}
    };
    
    // 购买按钮
    UIButton buy_button = {
        .rect = {460, 240, 100, 30},
        .text = "购买",
        .bg_color = {60, 120, 60, 255},
        .text_color = {255, 255, 255, 255},
        .hovered = 0,
        .pressed = 0
    };
    
    // 价格显示
    char price_text[64];
    sprintf(price_text, "价格: %s", app->item_prices[app->selected_item]);
    SDL_Surface* price_surface = TTF_RenderUTF8_Blended(app->font, price_text, (SDL_Color){255, 200, 100, 255});
    if (price_surface) {
        SDL_Texture* price_texture = SDL_CreateTextureFromSurface(app->renderer, price_surface);
        if (price_texture) {
            SDL_Rect price_rect = {460, 210, price_surface->w, price_surface->h};
            SDL_RenderCopy(app->renderer, price_texture, NULL, &price_rect);
            SDL_DestroyTexture(price_texture);
        }
        SDL_FreeSurface(price_surface);
    }
    
    // 渲染UI组件
    render_listbox(app->renderer, app->font, &product_list);
    render_dialog(app->renderer, app->font, &product_info);
    render_button(app->renderer, app->font, &buy_button);
    
    // 操作提示
    char help_text[128];
    sprintf(help_text, "操作: ↑↓选择 A/ENTER购买 ESC退出");
    SDL_Surface* help_surface = TTF_RenderUTF8_Blended(app->font, help_text, (SDL_Color){150, 150, 150, 255});
    if (help_surface) {
        SDL_Texture* help_texture = SDL_CreateTextureFromSurface(app->renderer, help_surface);
        if (help_texture) {
            SDL_Rect help_rect = {40, 400, help_surface->w, help_surface->h};
            SDL_RenderCopy(app->renderer, help_texture, NULL, &help_rect);
            SDL_DestroyTexture(help_texture);
        }
        SDL_FreeSurface(help_surface);
    }
    
    // 输入信息
    SDL_Surface* input_surface = TTF_RenderUTF8_Blended(app->font, app->input_info, (SDL_Color){120, 120, 120, 255});
    if (input_surface) {
        SDL_Texture* input_texture = SDL_CreateTextureFromSurface(app->renderer, input_surface);
        if (input_texture) {
            SDL_Rect input_rect = {40, 430, input_surface->w, input_surface->h};
            SDL_RenderCopy(app->renderer, input_texture, NULL, &input_rect);
            SDL_DestroyTexture(input_texture);
        }
        SDL_FreeSurface(input_surface);
    }
    
    // 帧数信息
    char frame_info[64];
    sprintf(frame_info, "Frame: %d", app->frame_count);
    SDL_Surface* frame_surface = TTF_RenderUTF8_Blended(app->font, frame_info, (SDL_Color){100, 100, 100, 255});
    if (frame_surface) {
        SDL_Texture* frame_texture = SDL_CreateTextureFromSurface(app->renderer, frame_surface);
        if (frame_texture) {
            SDL_Rect frame_rect = {app->width - 100, 20, frame_surface->w, frame_surface->h};
            SDL_RenderCopy(app->renderer, frame_texture, NULL, &frame_rect);
            SDL_DestroyTexture(frame_texture);
        }
        SDL_FreeSurface(frame_surface);
    }
    
    SDL_RenderPresent(app->renderer);
}

// 清理资源
void cleanup(tradeboy_context_t* app) {
    if (app->font) {
        TTF_CloseFont(app->font);
    }
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    TTF_Quit();
    SDL_Quit();
    log_message("SDL2 cleanup completed");
}

// 主函数
int main(int argc, char* argv[]) {
    tradeboy_context_t app = {0};
    
    // 设置窗口大小
    app.width = 640;
    app.height = 480;
    app.running = 1;  // 显式设置为运行状态
    
    // 打开日志
    open_log_file();
    log_message("TradeBoy application starting...");
    
    // 初始化SDL2
    if (!init_sdl2(&app)) {
        log_message("Failed to initialize SDL2");
        close_log_file();
        return 1;
    }
    
    // 初始化TradeBoy数据
    init_tradeboy_data(&app);
    
    // 初始化输入信息
    strcpy(app.input_info, "欢迎使用TradeBoy交易平台");
    app.last_input_time = time(NULL);
    
    log_message("=== Starting main loop ===");
    
    // 主循环
    while (app.running) {
        handle_events(&app);
        render_ui(&app);
        
        app.frame_count++;
        
        // 调试信息
        if (app.frame_count <= 5) {
            char debug_msg[128];
            sprintf(debug_msg, "Frame %d: running=%d", app.frame_count, app.running);
            log_message(debug_msg);
        }
        
        // 15秒无输入自动退出 (仅Linux环境)
        time_t current_time = time(NULL);
        int idle_time = current_time - app.last_input_time;
        #ifdef __linux__
            if (idle_time >= 15) {
                log_message("15 seconds without input - Auto exit for handheld device");
                app.running = 0;
            }
        #endif
        
        // 控制帧率 (30FPS)
        SDL_Delay(33);
    }
    
    // 清理
    cleanup(&app);
    close_log_file();
    
    printf("\n=== TradeBoy completed ===\n");
    printf("Total frames: %d\n", app.frame_count);
    
    return 0;
}
