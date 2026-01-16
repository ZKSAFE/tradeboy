#include "App.h"

#include <algorithm>
#include <sstream>
#include <chrono>

#include <SDL.h>

#include "imgui.h"

#include "../spot/SpotScreen.h"
#include "../spot/SpotUiEvents.h"
#include "../spotOrder/SpotOrderScreen.h"
#include "../market/MarketDataService.h"
#include "../market/HyperliquidWgetDataSource.h"
#include "../market/HyperliquidWsDataSource.h"
#include "../model/TradeModel.h"
#include "../utils/File.h"
#include "../utils/Math.h"
#include "../ui/MatrixBackground.h"
#include "../ui/MatrixTheme.h"

#include "../ui/Dialog.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::app {

static void render_placeholder_page(const char* title, ImFont* font_bold) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;

    const float padding = 16.0f;
    const float headerH = 54.0f;

    float left = p.x + padding;
    float right = p.x + size.x - padding;
    float y = p.y + padding;

    y += headerH;
    dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

    const char* msg = "IN DEVELOPMENT";
    ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(28.0f, FLT_MAX, 0.0f, msg) : ImGui::CalcTextSize(msg);
    float cx = p.x + (size.x - ts.x) * 0.5f;
    float cy = p.y + (size.y - ts.y) * 0.5f;
    if (font_bold) {
        dl->AddText(font_bold, 28.0f, ImVec2(cx, cy), MatrixTheme::DIM, msg);
    } else {
        dl->AddText(ImVec2(cx, cy), MatrixTheme::DIM, msg);
    }

    (void)title;
}

static const char* tab_title(Tab t) {
    switch (t) {
        case Tab::Spot: return "#SPOT";
        case Tab::Perp: return "#PERP";
        case Tab::Account: return "#ACCOUNT";
    }
    return "#SPOT";
}

static void render_main_header(Tab tab, bool l1_held, bool r1_held, ImFont* font_bold) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;
    if (size.x <= 1.0f || size.y <= 1.0f) return;

    const float padding = 16.0f;
    float left = p.x + padding;
    float right = p.x + size.x - padding;
    float y = p.y + padding;

    float headerDrawY = y - 10.0f;
    float titleSize = 42.0f;

    ImU32 glowCol = (MatrixTheme::TEXT & 0x00FFFFFF) | 0x40000000;
    const char* title = tab_title(tab);
    if (font_bold) {
        dl->AddText(font_bold, titleSize, ImVec2(left + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(font_bold, titleSize, ImVec2(left - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(font_bold, titleSize, ImVec2(left, headerDrawY), MatrixTheme::TEXT, title);
    } else {
        dl->AddText(ImVec2(left + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(ImVec2(left - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(ImVec2(left, headerDrawY), MatrixTheme::TEXT, title);
    }

    // Right nav: "SPOT | PERP | ACCOUNT" but current page shows as "*"
    const char* seg0 = (tab == Tab::Spot) ? "*" : "SPOT";
    const char* seg1 = (tab == Tab::Perp) ? "*" : "PERP";
    const char* seg2 = (tab == Tab::Account) ? "*" : "ACCOUNT";
    const char* sep = " | ";

    ImGui::SetWindowFontScale(1.0f);
    ImVec2 s0 = ImGui::CalcTextSize(seg0);
    ImVec2 s1 = ImGui::CalcTextSize(seg1);
    ImVec2 s2 = ImGui::CalcTextSize(seg2);
    ImVec2 sSep = ImGui::CalcTextSize(sep);

    // L1/R1 tags at far right
    ImGui::SetWindowFontScale(0.6f);
    float tagW = 24.0f;
    float tagH = 18.0f;
    float tagGap = 4.0f;
    float tagsW = tagW * 2.0f + tagGap;

    ImGui::SetWindowFontScale(1.0f);
    float navW = s0.x + sSep.x + s1.x + sSep.x + s2.x;

    float navY = headerDrawY + 8.0f;
    float navX = right - tagsW - 12.0f - navW;
    float x = navX;

    ImGui::SetWindowFontScale(1.0f);
    ImVec2 navBaselineSz = ImGui::CalcTextSize("SPOT");
    const float starYOffset = 4.0f;
    auto draw_seg = [&](const char* t, ImU32 col) {
        ImVec2 tsz = ImGui::CalcTextSize(t);
        float yy = navY;
        if (t[0] == '*' && t[1] == '\0') {
            yy = navY + (navBaselineSz.y - tsz.y) * 0.5f + starYOffset;
        }
        dl->AddText(ImVec2(x, yy), col, t);
        x += tsz.x;
    };

    draw_seg(seg0, (tab == Tab::Spot) ? MatrixTheme::TEXT : MatrixTheme::DIM);
    draw_seg(sep, MatrixTheme::DIM);
    draw_seg(seg1, (tab == Tab::Perp) ? MatrixTheme::TEXT : MatrixTheme::DIM);
    draw_seg(sep, MatrixTheme::DIM);
    draw_seg(seg2, (tab == Tab::Account) ? MatrixTheme::TEXT : MatrixTheme::DIM);

    // L1/R1 hints (vertically centered to nav text)
    ImGui::SetWindowFontScale(1.0f);
    ImVec2 navSz = ImGui::CalcTextSize("SPOT");
    float tagY = navY + (navSz.y - tagH) * 0.5f;

    ImU32 r1Bg = r1_held ? MatrixTheme::TEXT : MatrixTheme::DIM;
    float r1X = right - tagW;
    dl->AddRectFilled(ImVec2(r1X, tagY), ImVec2(r1X + tagW, tagY + tagH), r1Bg, 0.0f);
    ImGui::SetWindowFontScale(0.6f);
    ImVec2 r1Sz = ImGui::CalcTextSize("R1");
    dl->AddText(ImVec2(r1X + (tagW - r1Sz.x) * 0.5f, tagY + (tagH - r1Sz.y) * 0.5f), MatrixTheme::BLACK, "R1");

    ImU32 l1Bg = l1_held ? MatrixTheme::TEXT : MatrixTheme::DIM;
    float l1X = r1X - tagW - tagGap;
    dl->AddRectFilled(ImVec2(l1X, tagY), ImVec2(l1X + tagW, tagY + tagH), l1Bg, 0.0f);
    ImVec2 l1Sz = ImGui::CalcTextSize("L1");
    dl->AddText(ImVec2(l1X + (tagW - l1Sz.x) * 0.5f, tagY + (tagH - l1Sz.y) * 0.5f), MatrixTheme::BLACK, "L1");

    ImGui::SetWindowFontScale(1.0f);
}

void App::init_demo_data() {
    std::vector<tradeboy::model::SpotRow> rows = {
        {"BTC", 87482.75, 87482.75, 0.0, 87482.75},
        {"ETH", 2962.41, 2962.41, 0.0, 2962.41},
        {"SOL", 124.15, 124.15, 0.0, 124.15},
        {"BNB", 842.00, 842.00, 0.0, 842.00},
        {"XRP", 1.86, 1.86, 0.0, 1.86},
        {"TRX", 0.2843, 0.2843, 0.0, 0.2843},
        {"DOGE", 0.12907, 0.12907, 0.0, 0.12907},
        {"ADA", 0.3599, 0.3599, 0.0, 0.3599},
    };

    uint32_t seed = (uint32_t)(SDL_GetTicks() ^ 0xA53C9E17u);
    auto rnd01 = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFu) / 65535.0;
    };
    for (auto& r : rows) {
        double bal = 0.0;
        if (r.sym == "BTC") bal = 0.01 + rnd01() * 0.05;
        else if (r.sym == "ETH") bal = 0.2 + rnd01() * 1.5;
        else bal = rnd01() * 50.0;
        r.balance = bal;

        double entry_mul = 0.90 + rnd01() * 0.20;
        r.entry_price = r.price * entry_mul;
    }

    wallet_usdc = 10000.0;
    hl_usdc = 0.0;

    model.set_spot_rows(std::move(rows));
    model.set_spot_row_idx(spot_row_idx);
}

void App::startup() {
    if (!market_src) {
        log_to_file("[App] Market source: WS (forced)\n");
        market_src.reset(new tradeboy::market::HyperliquidWsDataSource());
    }
    if (!market_service) {
        market_service.reset(new tradeboy::market::MarketDataService(model, *market_src));
    }
    market_service->start();
}

void App::shutdown() {
    log_to_file("[App] shutdown()\n");
    if (market_service) {
        market_service->stop();
        market_service.reset();
    }
    market_src.reset();
}

void App::load_private_key() {
    log_to_file("[App] load_private_key start, this=%p\n", this);
    const char* path = "./data/private_key.txt";
    
    log_to_file("[App] reading file: %s\n", path);
    std::string raw = tradeboy::utils::read_text_file(path);
    log_to_file("[App] read_text_file done, size=%d\n", (int)raw.size());

    if (raw.empty()) {
        log_to_file("[App] private key missing or empty: %s\n", path);
        
        log_to_file("[App] clearing priv_key_hex...\n");
        priv_key_hex = ""; // Try assignment instead of clear()
        log_to_file("[App] priv_key_hex cleared\n");
        
        log_to_file("[App] load_private_key early return\n");
        return;
    }
    
    log_to_file("[App] private key raw_len=%d\n", (int)raw.size());
    priv_key_hex = tradeboy::utils::normalize_hex_private_key(raw);
    log_to_file("[App] private key normalized_len=%d\n", (int)priv_key_hex.size());
}

void App::dec_frame_counter(int& v) {
    if (v > 0) v--;
}

void App::open_spot_order(bool buy) {
    tradeboy::model::TradeModelSnapshot snap = model.snapshot();
    if (snap.spot_rows.empty()) return;
    if (spot_row_idx < 0 || spot_row_idx >= (int)snap.spot_rows.size()) return;
    const auto& row = snap.spot_rows[(size_t)spot_row_idx];
    double maxv = 0.0;
    if (buy) {
        maxv = (row.price > 0.0) ? (hl_usdc / row.price) : 0.0;
    } else {
        maxv = row.balance;
    }

    tradeboy::spotOrder::Side side = buy ? tradeboy::spotOrder::Side::Buy : tradeboy::spotOrder::Side::Sell;
    spot_order.open_with(row, side, maxv);
}

void App::apply_spot_ui_events(const std::vector<tradeboy::spot::SpotUiEvent>& ev) {
    for (const auto& e : ev) {
        switch (e.type) {
            case tradeboy::spot::SpotUiEventType::RowDelta:
                spot_row_idx += e.value;
                model.set_spot_row_idx(spot_row_idx);
                break;
            case tradeboy::spot::SpotUiEventType::EnterActionFocus:
                spot_action_focus = true;
                spot_action_idx = e.value;
                break;
            case tradeboy::spot::SpotUiEventType::ExitActionFocus:
                spot_action_focus = false;
                break;
            case tradeboy::spot::SpotUiEventType::SetActionIdx:
                spot_action_idx = e.value;
                break;
            case tradeboy::spot::SpotUiEventType::TriggerAction:
                if (e.flag) {
                    buy_trigger_frames = 24;
                } else {
                    sell_trigger_frames = 24;
                }
                break;
        }
    }
}

void App::handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (quit_requested) return;

    // Exit modal has highest priority.
    if (exit_modal_open) {
        if (exit_dialog_closing) {
            return;
        }

        if (exit_dialog_flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                exit_dialog_selected_btn = 1 - exit_dialog_selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                exit_dialog_pending_action = exit_dialog_selected_btn;
                exit_dialog_flash_frames = 24;
            }
            if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
                exit_dialog_closing = true;
                exit_dialog_close_frames = 0;
                exit_dialog_quit_after_close = false;
                exit_dialog_flash_frames = 0;
                exit_dialog_pending_action = -1;
            }
        }
        return;
    }

    if (tradeboy::utils::pressed(in.m, edges.prev.m)) {
        exit_modal_open = true;
        exit_dialog_selected_btn = 1;
        exit_dialog_open_frames = 0;
        exit_dialog_flash_frames = 0;
        exit_dialog_pending_action = -1;
        exit_dialog_closing = false;
        exit_dialog_close_frames = 0;
        exit_dialog_quit_after_close = false;
        return;
    }

    if (tradeboy::spotOrder::handle_input(spot_order, in, edges)) {
        return;
    }

    // Global tab switching (only when not inside order modal)
    if (!spot_order.open) {
        if (tradeboy::utils::pressed(in.l1, edges.prev.l1)) {
            l1_flash_frames = 6;
            if (tab == Tab::Spot) tab = Tab::Account;
            else if (tab == Tab::Perp) tab = Tab::Spot;
            else tab = Tab::Perp;
            return;
        }
        if (tradeboy::utils::pressed(in.r1, edges.prev.r1)) {
            r1_flash_frames = 6;
            if (tab == Tab::Spot) tab = Tab::Perp;
            else if (tab == Tab::Perp) tab = Tab::Account;
            else tab = Tab::Spot;
            return;
        }
    }
    
    // Block spot input if triggering action
    if (buy_trigger_frames > 0 || sell_trigger_frames > 0) {
        return;
    }

    // Update held states for UI feedback
    action_btn_held = in.a;
    l1_btn_held = in.l1;
    r1_btn_held = in.r1;

    if (tab == Tab::Spot) {
        tradeboy::spot::SpotUiState ui;
        ui.spot_action_focus = spot_action_focus;
        ui.spot_action_idx = spot_action_idx;
        ui.buy_press_frames = buy_press_frames;
        ui.sell_press_frames = sell_press_frames;

        std::vector<tradeboy::spot::SpotUiEvent> ev = tradeboy::spot::collect_spot_ui_events(in, edges, ui);
        apply_spot_ui_events(ev);
    }
}

void App::render() {
    // Process triggers
    if (buy_trigger_frames > 0) {
        buy_trigger_frames--;
        if (buy_trigger_frames == 0) open_spot_order(true);
    }
    if (sell_trigger_frames > 0) {
        sell_trigger_frames--;
        if (sell_trigger_frames == 0) open_spot_order(false);
    }

    // Flash logic: faster blink while trigger is active.
    // Total trigger duration is still 45 frames, but blink period is shorter.
    const int blinkPeriod = 6;
    const int blinkOnFrames = 3;
    bool buy_flash = (buy_trigger_frames > 0) && ((buy_trigger_frames % blinkPeriod) < blinkOnFrames);
    bool sell_flash = (sell_trigger_frames > 0) && ((sell_trigger_frames % blinkPeriod) < blinkOnFrames);

    // Header L1/R1 is one-shot: highlight once, like SpotOrderScreen.
    bool l1_flash = (l1_flash_frames > 0);
    bool r1_flash = (r1_flash_frames > 0);

    tradeboy::spot::SpotUiState ui;
    ui.spot_action_focus = spot_action_focus;
    ui.spot_action_idx = spot_action_idx;
    ui.buy_press_frames = buy_press_frames;
    ui.sell_press_frames = sell_press_frames;

    // Shared background layer (grid)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        tradeboy::ui::render_matrix_grid(dl, p, size);
    }

    // Main header for top-level tabs (Spot/Perp/Account)
    if (!spot_order.open) {
        render_main_header(tab, l1_flash, r1_flash, font_bold);
    }

    // Spot page now uses the new UI demo layout. Data layer is intentionally
    // not connected yet (render uses mock data only).
    // Hide Spot page while order page is open to avoid overlap.
    if (!spot_order.open) {
        if (tab == Tab::Spot) {
            tradeboy::spot::render_spot_screen(
                spot_row_idx,
                spot_action_idx,
                buy_flash,
                sell_flash,
                font_bold,
                action_btn_held,
                l1_btn_held,
                r1_btn_held);
        } else if (tab == Tab::Perp) {
            render_placeholder_page("PERP", font_bold);
        } else {
            render_placeholder_page("ACCOUNT", font_bold);
        }
    }

    dec_frame_counter(buy_press_frames);
    dec_frame_counter(sell_press_frames);
    dec_frame_counter(l1_flash_frames);
    dec_frame_counter(r1_flash_frames);
    tradeboy::spotOrder::render(spot_order, font_bold);

    // Process exit dialog flash -> trigger closing when finished.
    if (exit_modal_open && !exit_dialog_closing && exit_dialog_flash_frames > 0) {
        exit_dialog_flash_frames--;
        if (exit_dialog_flash_frames == 0 && exit_dialog_pending_action >= 0) {
            exit_dialog_closing = true;
            exit_dialog_close_frames = 0;
            exit_dialog_quit_after_close = (exit_dialog_pending_action == 0);
            exit_dialog_pending_action = -1;
        }
    }

    // Close animation
    if (exit_modal_open && exit_dialog_closing) {
        const int close_dur = 18;
        exit_dialog_close_frames++;
        if (exit_dialog_close_frames >= close_dur) {
            exit_modal_open = false;
            exit_dialog_closing = false;
            exit_dialog_close_frames = 0;
            if (exit_dialog_quit_after_close) {
                quit_requested = true;
            }
            exit_dialog_quit_after_close = false;
        }
    }

    overlay_rect_active = false;
    overlay_rect_uv = ImVec4(0, 0, 0, 0);

    if (exit_modal_open) {
        float open_t = 1.0f;
        if (!exit_dialog_closing) {
            if (exit_dialog_open_frames < 18) exit_dialog_open_frames++;
            open_t = (float)exit_dialog_open_frames / 18.0f;
        } else {
            const int close_dur = 18;
            open_t = 1.0f - (float)exit_dialog_close_frames / (float)close_dur;
        }

        tradeboy::ui::render_dialog("ExitDialog",
                                    "> ",
                                    "Exit TradeBoy?",
                                    "EXIT",
                                    "CANCEL",
                                    &exit_dialog_selected_btn,
                                    exit_dialog_flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }
}

} // namespace tradeboy::app
