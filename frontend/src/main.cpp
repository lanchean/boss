/*
    IdleBossHunter client
    Copyright (C) 2020  Michael de Lang

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/websocket.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <chrono>
#include <memory>
#include "spdlog/spdlog.h"
#include <memory>
#include <rendering/imgui/imgui.h>
#include <rendering/imgui/imgui_impl_sdl.h>
#include <rendering/imgui/imgui_impl_opengl3.h>
#include <ecs/ecs.h>
#include <ecs/systems/rendering_system.h>
#include <ecs/components.h>
#include <ecs/systems/scene_system.h>
#include <thread>
#include <numeric>

//#include "threadpool.h"
#include "rendering/shader_utils.h"
#include "timer.h"
#include "rendering/sprite.h"
#include "rendering/texture_atlas.h"
#include "config.h"
//#include "map_layout_config.h"
#include "map_loading/map.h"
#include "init/sdl_init.h"
#include "init/logger_init.h"
#include "scenes/gui_scenes/connection_lost_scene.h"
#include <messages/user_access/login_request.h>
#include <common_components.h>
#include "config_parsers.h"
#include "networking.h"

using namespace std;
using namespace ibh;

namespace ibh {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    glm::mat4 projection;
}

ImGuiIO& init_imgui() {
    if(!IMGUI_CHECKVERSION()) {
        spdlog::error("[main] Error with imgui check version.");
        exit(1);
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("assets/fonts/TheanoDidot-Regular.ttf", 20);

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init(nullptr);

    return io;
}

void set_threads_config(config& config) {
    int threads = 0;
    if(config.threads <= 0) {
        threads = thread::hardware_concurrency();

        if(threads == 0) {
            throw runtime_error("Could not auto-detect number of cpu cores. Please configure threads setting manually.");
        }

        config.threads = threads;
        spdlog::info("[main] auto-detected {} cores, creating {} background threads", threads, threads);
    } else {
        threads = config.threads;
        spdlog::info("[main] creating {} background threads", threads);
    }

    if(threads < 8) {
        spdlog::warn("[main] less than 8 threads configured, unlikely to perform well. Consider upgrading your CPU");
    }
}

void close(atomic<bool> &quit) noexcept
{
#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
    SDL_StopTextInput();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    window = nullptr;

    Mix_CloseAudio();

    IMG_Quit();
    Mix_Quit();
    SDL_Quit();
    quit = true;
}

void set_working_dir() noexcept {
    char *base_path = SDL_GetBasePath();
    if (base_path) {
        spdlog::info("[main] Set base_path to {}", base_path);
        if(chdir(base_path) != 0) {
            spdlog::warn("[main] chdir returned error {}", errno);
        }
        SDL_free(base_path);
    } else {
        spdlog::error("[main] Couldn't get base path: {}", SDL_GetError());
        exit(1);
    }

}

//using message_router_type = ibh_flat_map<string, function<void(rapidjson::Document const &)>>;


std::function<void()> loop;
void main_loop() { loop(); }

int main(int argc, char* argv[]) {
    set_working_dir();

    config config{};
#ifdef __EMSCRIPTEN__
    config.debug_level = "trace";
    config.tick_length = 50;
    config.log_fps = true;
    config.screen_width = 1600;
    config.screen_height = 900;
    config.server_url = "wss://www.realmofaesir.com:8080/";
#else
    try {
        auto config_opt = parse_env_file();
        if(!config_opt) {
            return 1;
        }
        config = config_opt.value();
    } catch (const exception& e) {
        spdlog::error("[main] config/game_config.json file is malformed json.");
        return 1;
    }
#endif

    reconfigure_logger(config);
    config.music_to_play = 1;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop([]{ /* Only setting main loop so that an emscripten warning gets quenched */ }, 0, 0);
#endif

    fill_mappers();
    entt::registry es{};
    scene_system ss(&config, es);

    init_sdl(config);
    init_sdl_image();
    init_sdl_mixer(config);

    set_threads_config(config);
    /*auto& io =*/ init_imgui();

    timer<microseconds> fps_timer{};
    timer<microseconds> tick_timer{};
    timer<microseconds> bench_timer{};
    vector<uint64_t> frame_times{};
    frame_times.reserve(config.refresh_rate);
    int counted_frames = 0;
    //ThreadPool thread_pool(config.threads);

    atomic<bool> quit = false;

    Mix_Music *mus1 = Mix_LoadMUS("assets/music/8bit Stage1 Intro.ogg");
    if(mus1 == nullptr) {
        spdlog::error("Couldn't load mus1, {}", Mix_GetError());
        close(quit);
        return -1;
    }

    Mix_Music *mus2 = Mix_LoadMUS("assets/music/8bit Stage1 Loop.ogg");
    if(mus2 == nullptr) {
        spdlog::error("Couldn't load mus1, {}", Mix_GetError());
        close(quit);
        return -1;
    }
    Mix_PlayMusic(mus1, 0);

    //auto map = ibh::map::load_from_file("./assets/maps/");

    fps_timer.start();
    tick_timer.start();

    rendering_system rs(&config, window, context);
    ss.init_connection_screen();
    bool previousCapture = false;

#ifdef __EMSCRIPTEN__
    init_net(config, es, ss);
#else
    auto net_thread = init_net(config, es, ss);
#endif

    loop = [&] {
        try {
            SDL_Event e;

            while (SDL_PollEvent(&e) != 0) {
                ImGui_ImplSDL2_ProcessEvent(&e);

                ImGuiIO& io = ImGui::GetIO();
                if(previousCapture != io.WantCaptureKeyboard) {
                    if (io.WantCaptureKeyboard) {
                        SDL_StartTextInput();
                    } else {
                        SDL_StopTextInput();
                    }
                    previousCapture = io.WantCaptureKeyboard;
                }

                switch (e.type) {
                    case SDL_KEYDOWN: {
                        if (io.KeyCtrl && e.key.keysym.sym == SDLK_p) {
                            if (Mix_PlayingMusic()) {
                                if (Mix_PausedMusic()) {
                                    Mix_ResumeMusic();
                                } else {
                                    Mix_PauseMusic();
                                }
                            } else {
                                Mix_PlayMusic(mus1, 0);
                            }
                        }
                        if(e.key.keysym.sym == SDLK_MINUS) {
                            auto volume = Mix_Volume(-1,-1);
                            volume -= 5;
                            Mix_Volume(-1, volume);
                            Mix_VolumeMusic(volume);
                        }
                        if(e.key.keysym.sym == SDLK_EQUALS) {
                            auto volume = Mix_Volume(-1,-1);
                            volume += 5;
                            Mix_Volume(-1, volume);
                            Mix_VolumeMusic(volume);
                        }
#ifndef __EMSCRIPTEN__
                        if(e.key.keysym.sym == SDLK_ESCAPE) {
                            quit.store(true, memory_order_release);
                        }
#endif
                        break;
                    }
                    case SDL_WINDOWEVENT: {
                        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                            config.screen_width = e.window.data1;
                            config.screen_height = e.window.data2;
                            spdlog::info("Resize to {}x{}", config.screen_width, config.screen_height);
                            projection = glm::ortho(0.0F, (float) config.screen_width, (float) config.screen_height,
                                                    0.0F, -1.0F, 1.0F);
#ifndef __EMSCRIPTEN__
                            glViewport(0, 0, config.screen_width, config.screen_height);
#endif
                        }
                        break;
                    }
                    case SDL_QUIT:
                        quit.store(true, memory_order_release);
                        break;
                    default: {
                        if (e.type == config.user_event_type || e.type == SDL_USEREVENT) {
                            spdlog::info("userevent {}", e.user.code);
                            switch (e.user.code) {
                                case 0: {
                                    config.screen_width = 1280;
                                    config.screen_height = 720;
                                    projection = glm::ortho(0.0F, 1280.F, 720.F, 0.0F, -1.0F, 1.0F);
                                    SDL_SetWindowSize(window, 1280, 720);
                                    break;
                                }
                                case 1: {
                                    config.screen_width = 1600;
                                    config.screen_height = 900;
                                    projection = glm::ortho(0.0F, 1600.F, 900.F, 0.0F, -1.0F, 1.0F);
                                    SDL_SetWindowSize(window, 1600, 900);
                                    break;
                                }
                                case 2: {
                                    config.screen_width = 1920;
                                    config.screen_height = 1080;
                                    projection = glm::ortho(0.0F, 1920.F, 1080.F, 0.0F, -1.0F, 1.0F);
                                    SDL_SetWindowSize(window, 1920, 1080);
                                    break;
                                }
                                case 3: {
                                    Mix_HaltMusic();
                                    Mix_FreeMusic(mus1);
                                    Mix_FreeMusic(mus2);

                                    int *val = static_cast<int *>(e.user.data1);

                                    mus1 = Mix_LoadMUS(
                                            fmt::format("assets/music/8bit Stage{} Intro.ogg", *val).c_str());
                                    if (mus1 == nullptr) {
                                        spdlog::error("Couldn't load mus1, {}", Mix_GetError());
                                        close(quit);
                                    }

                                    mus2 = Mix_LoadMUS(fmt::format("assets/music/8bit Stage{} Loop.ogg", *val).c_str());
                                    if (mus2 == nullptr) {
                                        spdlog::error("Couldn't load mus1, {}", Mix_GetError());
                                        close(quit);
                                    }
                                    Mix_PlayMusic(mus1, 0);
                                    config.music_to_play = *val;
                                    delete val;
                                    break;
                                }
                                case 4: {
                                    bool *val = static_cast<bool *>(e.user.data1);
                                    SDL_SetWindowBordered(window, *val ? SDL_FALSE : SDL_TRUE);
                                    delete val;
                                    break;
                                }
                                case 10:
                                    Mix_Volume(-1, config.volume);
                                    Mix_VolumeMusic(config.volume);
                                    break;
                                default:
                                    break;
                            }
                        }
                        break;
                    }
                }
            }

            if (Mix_PlayingMusic() == 0) {
                Mix_PlayMusic(mus2, -1);
            }

            auto prev = bench_timer.get_ticks();
            bench_timer.start();
            rs.update(es, prev);
            ss.update(es, prev);
            rs.end_rendering();

            if (config.log_fps) {
                frame_times.push_back(bench_timer.get_ticks());
            }

            rs.swap_window();

            ++counted_frames;

            auto fps_ticks = fps_timer.get_ticks();
            if (config.log_fps && fps_ticks >= 2'000'000) {
                spdlog::info("[main] FPS {} - frame times max/avg/min: {} / {} / {} ??s", counted_frames / 2,
                             *max_element(begin(frame_times), end(frame_times)),
                             accumulate(begin(frame_times), end(frame_times), 0UL) / frame_times.size(),
                             *min_element(begin(frame_times), end(frame_times)));
                fps_timer.start();
                frame_times.clear();
                counted_frames = 0;
            }
        } catch (exception const &e) {
            spdlog::error("[main] exception {}", e.what());
        }
    };

#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    while (!quit.load(memory_order_acquire)) { main_loop(); }
#endif

    spdlog::info("quitting");

    Mix_FreeMusic(mus1);
    Mix_FreeMusic(mus2);

#ifndef __EMSCRIPTEN__
    if(ss.get_socket().running && !ss.get_socket().socket->stopped()) {
        ss.get_socket().socket->stop();
    }
    if(net_thread.joinable()) {
        net_thread.join();
    }
#endif

    close(quit);

    return 0;
}
