#include "./variant.h"
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <simple-2d/2d.h>
#include <simple-2d/image.h>
#include <simple-2d/setup.h>
#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// #include <discord_game_sdk/discord.h> // last because it includes windows.h
// which has SO MANY macros
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXUserAgent.h>
#include <ixwebsocket/IXWebSocket.h>

using namespace std::string_view_literals;
const char *title = "discord vc pfps :3";
int width = 500;
int height = 150;
std::unique_ptr<ix::HttpClient> httpClient = nullptr;
ix::WebSocket webSocket{};

struct User {
  Image image;
  uint64_t id;
  uint64_t channel_id;
  std::optional<double> speaking = std::nullopt;
  bool muted = false;
  bool deafened = false;

  bool loadAvatar(std::string avatar) {
    if (image.width != 0)
      return false;
    auto request = httpClient->createRequest(
        "https://cdn.discordapp.com/avatars/" + std::to_string(id) + "/" +
            avatar + ".png",
        ix::HttpClient::kGet);
    return httpClient->performRequest(
        request, [&](const ix::HttpResponsePtr &response) {
          if (response->statusCode != 200) {
            std::cerr << "failed to get avatar for user " << id << ": "
                      << response->statusCode << " - " << response->body
                      << std::endl;
            exit(1);
          }
          std::vector<uint8_t> bytes(response->body.begin(),
                                     response->body.end());
          auto image = decode_image(bytes);
          if (!image) {
            std::cerr << "failed to load image for user " << id << std::endl;
            exit(1);
          }
          this->image = std::move(*image);
          std::cout << "loaded avatar for user " << id << std::endl;
        });
  }
};

std::map<uint64_t, User> all_users;
User &user(uint64_t id) {
  auto it = all_users.find(id);
  if (it == all_users.end()) {
    auto &user = all_users[id];
    user.id = id;
    return user;
  }
  return it->second;
}

std::string_view to_string_view(const rapidjson::Value &json) {
  return std::string_view(json.GetString(), json.GetStringLength());
}
std::string to_string(const rapidjson::Value &json) {
  return std::string(json.GetString(), json.GetStringLength());
}

namespace discord {
struct UserInfo {
  uint64_t user_id;
  std::string avatar;
};
constexpr uint64_t CLIENT_ID = 207646673902501888;
std::string nonce() {
  // i suppose.
  static uint64_t nonce = 0;
  return std::to_string(++nonce);
}
std::string code;
std::string access_token;
std::optional<uint64_t> channel_id;

struct Authorize {
  std::string nonce = discord::nonce();
  std::string json() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("cmd");
    writer.String("AUTHORIZE");
    writer.Key("args");
    writer.StartObject();
    writer.Key("client_id");
    writer.String(std::to_string(CLIENT_ID));
    writer.Key("scopes");
    writer.StartArray();
    writer.String("rpc");
    writer.String("messages.read");
    writer.EndArray();
    writer.Key("prompt");
    writer.String("none");
    writer.EndObject();
    writer.Key("nonce");
    writer.String(nonce);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
  }

  struct Response {
    std::string code;
  };
};

struct Authenticate {
  std::string access_token;
  std::string nonce = discord::nonce();
  std::string json() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("cmd");
    writer.String("AUTHENTICATE");
    writer.Key("args");
    writer.StartObject();
    writer.Key("access_token");
    writer.String(access_token);
    writer.EndObject();
    writer.Key("nonce");
    writer.String(nonce);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
  }

  struct Response : public UserInfo {};
};

struct GetSelectedVoiceChannel {
  std::string nonce = discord::nonce();
  std::string json() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("cmd");
    writer.String("GET_SELECTED_VOICE_CHANNEL");
    writer.Key("nonce");
    writer.String(nonce);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
  }

  struct ResponseIfNotNull {
    uint64_t channel_id;
    uint64_t guild_id;
    std::vector<UserInfo> users;
  };
  using Response = std::optional<ResponseIfNotNull>;
};

struct Subscribe {
  std::string evt;
  std::optional<uint64_t> channel_id;
  std::string nonce = discord::nonce();
  std::string json() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("cmd");
    writer.String("SUBSCRIBE");
    writer.Key("args");
    writer.StartObject();
    if (channel_id.has_value()) {
      writer.Key("channel_id");
      writer.String(std::to_string(*channel_id));
    }
    writer.EndObject();
    writer.Key("evt");
    writer.String(evt);
    writer.Key("nonce");
    writer.String(nonce);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
  }

  struct Response {};
};

struct Unsubscribe {
  std::string evt;
  std::optional<uint64_t> channel_id;
  std::string nonce = discord::nonce();
  std::string json() {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("cmd");
    writer.String("UNSUBSCRIBE");
    writer.Key("args");
    writer.StartObject();
    if (channel_id.has_value()) {
      writer.Key("channel_id");
      writer.String(std::to_string(*channel_id));
    }
    writer.EndObject();
    writer.Key("evt");
    writer.String(evt);
    writer.Key("nonce");
    writer.String(nonce);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
  }

  struct Response {};
};

namespace dispatch {
struct Ready {
  int v;
  // std::string cdn_host;
  // std::string api_endpoint;
  // std::string environment;
};
struct VoiceStateCreate : UserInfo {
  VoiceStateCreate(UserInfo user, bool mute, bool deaf)
      : UserInfo(user), mute(mute), deaf(deaf) {}
  bool mute;
  bool deaf;
};
struct VoiceStateUpdate {
  uint64_t user_id;
  bool mute;
  bool deaf;
};
struct VoiceStateDelete {
  uint64_t user_id;
};
struct SpeakingStart {
  uint64_t channel_id;
  uint64_t user_id;
};
struct SpeakingStop {
  uint64_t channel_id;
  uint64_t user_id;
};
struct VoiceChannelSelectIfNotNull {
  uint64_t channel_id;
  uint64_t guild_id;
};
using VoiceChannelSelect = std::optional<VoiceChannelSelectIfNotNull>;
} // dispatch

using Response = variant<std::monostate, // none
                         Authorize::Response, Authenticate::Response,
                         Subscribe::Response, Unsubscribe::Response,
                         GetSelectedVoiceChannel::Response, dispatch::Ready,
                         dispatch::VoiceStateCreate, dispatch::VoiceStateUpdate,
                         dispatch::VoiceStateDelete, dispatch::SpeakingStart,
                         dispatch::SpeakingStop, dispatch::VoiceChannelSelect>;

Response parse_response(const std::string &str) {
  rapidjson::Document json{};
  json.Parse(str);
  if (json.HasParseError()) {
    std::cerr << "failed to parse json" << std::endl;
    return std::monostate{};
  }
  if (!json.IsObject()) {
    std::cerr << "json is not an object" << std::endl;
    return std::monostate{};
  }
  auto fcmd = json.FindMember("cmd");
  if (fcmd == json.MemberEnd()) {
    std::cerr << "json has no cmd" << std::endl;
    return std::monostate{};
  }
  auto cmd = to_string_view(fcmd->value);
  if (cmd == "AUTHORIZE") {
    return Authorize::Response{.code = to_string(json["data"]["code"])};
  } else if (cmd == "AUTHENTICATE") {
    return Authenticate::Response{UserInfo{
        .user_id = std::stoull(to_string(json["data"]["user"]["id"])),
        .avatar = to_string(json["data"]["user"]["avatar"]),
    }};
  } else if (cmd == "SUBSCRIBE") {
    return Subscribe::Response{};
  } else if (cmd == "UNSUBSCRIBE") {
    return Unsubscribe::Response{};
  } else if (cmd == "GET_SELECTED_VOICE_CHANNEL") {
    if (json["data"].IsNull() || json["data"]["id"].IsNull())
      return GetSelectedVoiceChannel::Response{};
    std::vector<UserInfo> users;
    for (auto &voice_state : json["data"]["voice_states"].GetArray())
      users.push_back(UserInfo{
          .user_id = std::stoull(to_string(voice_state["user"]["id"])),
          .avatar = to_string(voice_state["user"]["avatar"]),
      });
    return GetSelectedVoiceChannel::ResponseIfNotNull{
        .channel_id = std::stoull(to_string(json["data"]["id"])),
        .guild_id = std::stoull(to_string(json["data"]["guild_id"])),
        .users = std::move(users),
    };
  } else if (cmd != "DISPATCH")
    return std::monostate{};
  // start dispatch
  auto fevt = json.FindMember("evt");
  if (fevt == json.MemberEnd()) {
    std::cerr << "json has no evt" << std::endl;
    return std::monostate{};
  }
  auto evt = to_string_view(fevt->value);
  if (evt == "READY") {
    return dispatch::Ready{.v = json["data"]["v"].GetInt()};
  } else if (evt == "VOICE_STATE_CREATE") {
    return dispatch::VoiceStateCreate(
        UserInfo{
            .user_id = std::stoull(to_string(json["data"]["user"]["id"])),
            .avatar = to_string(json["data"]["user"]["avatar"]),
        },
        json["data"]["voice_state"]["mute"].GetBool(),
        json["data"]["voice_state"]["deaf"].GetBool());
  } else if (evt == "VOICE_STATE_UPDATE") {
    return dispatch::VoiceStateUpdate{
        .user_id = std::stoull(to_string(json["data"]["user"]["id"])),
        .mute = json["data"]["voice_state"]["mute"].GetBool(),
        .deaf = json["data"]["voice_state"]["deaf"].GetBool()};
  } else if (evt == "VOICE_STATE_DELETE") {
    return dispatch::VoiceStateDelete{
        .user_id = std::stoull(to_string(json["data"]["user"]["id"]))};
  } else if (evt == "SPEAKING_START") {
    return dispatch::SpeakingStart{
        .channel_id = std::stoull(to_string(json["data"]["channel_id"])),
        .user_id = std::stoull(to_string(json["data"]["user_id"])),
    };
  } else if (evt == "SPEAKING_STOP") {
    return dispatch::SpeakingStop{
        .channel_id = std::stoull(to_string(json["data"]["channel_id"])),
        .user_id = std::stoull(to_string(json["data"]["user_id"])),
    };
  } else if (evt == "VOICE_CHANNEL_SELECT") {
    if (json["data"]["channel_id"].IsNull())
      return dispatch::VoiceChannelSelect{};
    return dispatch::VoiceChannelSelectIfNotNull{
        .channel_id = std::stoull(to_string(json["data"]["channel_id"])),
        .guild_id = std::stoull(to_string(json["data"]["guild_id"])),
    };
  }
  return std::monostate{};
}

void change_the_channel(std::optional<uint64_t> new_channel) {
  if (channel_id == new_channel)
    return;
  if (channel_id.has_value()) {
    webSocket.sendText(
        Unsubscribe{.evt = "VOICE_STATE_CREATE", .channel_id = channel_id}
            .json());
    webSocket.sendText(
        Unsubscribe{.evt = "VOICE_STATE_UPDATE", .channel_id = channel_id}
            .json());
    webSocket.sendText(
        Unsubscribe{.evt = "VOICE_STATE_DELETE", .channel_id = channel_id}
            .json());
    webSocket.sendText(
        Unsubscribe{.evt = "SPEAKING_START", .channel_id = channel_id}.json());
    webSocket.sendText(
        Unsubscribe{.evt = "SPEAKING_STOP", .channel_id = channel_id}.json());
  }
  if (new_channel.has_value()) {
    webSocket.sendText(
        Subscribe{.evt = "VOICE_STATE_CREATE", .channel_id = new_channel}
            .json());
    webSocket.sendText(
        Subscribe{.evt = "VOICE_STATE_UPDATE", .channel_id = new_channel}
            .json());
    webSocket.sendText(
        Subscribe{.evt = "VOICE_STATE_DELETE", .channel_id = new_channel}
            .json());
    webSocket.sendText(
        Subscribe{.evt = "SPEAKING_START", .channel_id = new_channel}.json());
    webSocket.sendText(
        Subscribe{.evt = "SPEAKING_STOP", .channel_id = new_channel}.json());
  }
  channel_id = new_channel;
}
}

void init(On on) {
  std::filesystem::path path = std::filesystem::current_path() / "people";
  if (!std::filesystem::is_directory(path)) {
    std::cerr << "people directory not found" << std::endl;
    exit(1);
  }
  for (auto &entry : std::filesystem::directory_iterator(path)) {
    if (entry.is_regular_file()) {
      auto filename = entry.path();
      auto stem = filename.stem().string();
      if (filename.extension() != ".png")
        continue;
      size_t underscore = stem.find('_');
      uint64_t id;
      if (underscore == std::string::npos)
        id = std::stoull(stem);
      else
        id = std::stoull(stem.substr(underscore + 1));
      auto image = load_image(filename.string());
      if (!image) {
        std::cerr << "failed to load image at " << filename << std::endl;
        exit(1);
      }
      user(id).image = std::move(*image);
      std::cout << "loaded " << filename << std::endl;
    } else {
      std::cerr << "unexpected file type at " << entry.path() << std::endl;
      exit(1);
    }
  }
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

  // Required on Windows
  ix::initNetSystem();

  // Our websocket object
  ::httpClient = std::make_unique<ix::HttpClient>(true);
  std::string url("ws://127.0.0.1:6463/?v=1&client_id=207646673902501888");
  // todo: https://discord.com/developers/docs/topics/rpc
  // todo: ws://127.0.0.1:6463/?v=1&client_id=207646673902501888
  // WAIT LMAOOOO THATS NOT EVEN ENCRYPTED .
  // todo remove mbedtls?????? probs not just in case
  webSocket.setUrl(url);
  webSocket.setExtraHeaders({
      {"Host", "127.0.0.1:6463"},
      {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) "
                     "Gecko/20100101 Firefox/119.0"},
      {"Accept", "*/*"},
      {"Accept-Language", "en-GB,en;q=0.7,nl;q=0.3"},
      {"Prefer", "safe"},
      {"Origin", "https://streamkit.discord.com"},
      {"DNT", "1"},
      {"Connection", "keep-alive, Upgrade"},
      {"Pragma", "no-cache"},
      {"Cache-Control", "no-cache"},
  });
  webSocket.disableAutomaticReconnection();

  std::cout << "Connecting to " << url << "..." << std::endl;

  webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
    std::cout << "received message" << std::endl;
    if (msg->type == ix::WebSocketMessageType::Message) {
      std::cout << "received message: " << msg->str << std::endl;
      std::cout << "> " << std::flush;
      using namespace discord;
      auto response = parse_response(msg->str);
      response.visit(
          [&](std::monostate) {
            std::cerr << "got empty response" << std::endl;
            exit(1);
          },
          [&](Authorize::Response response) {
            code = std::move(response.code);
            std::cout << "got code " << code << std::endl;
            auto request = httpClient->createRequest(
                "https://streamkit.discord.com/overlay/token",
                ix::HttpClient::kPost);
            request->body = "{\"code\":\"" + code + "\"}";
            httpClient->performRequest(
                request, [&](const ix::HttpResponsePtr &response) {
                  if (response->statusCode != 200) {
                    std::cerr << "failed to get token with code " << code
                              << ": " << response->statusCode << " - "
                              << response->body << std::endl;
                    exit(1);
                  }
                  rapidjson::Document json{};
                  json.Parse(response->body);
                  if (json.HasParseError()) {
                    std::cerr << "failed to parse json" << std::endl;
                    exit(1);
                  }
                  if (!json.IsObject()) {
                    std::cerr << "json is not an object" << std::endl;
                    exit(1);
                  }
                  access_token = to_string(json["access_token"]);
                  std::cout << "got access token " << access_token << std::endl;
                  Authenticate authenticate{.access_token = access_token};
                  webSocket.sendText(authenticate.json());
                });
          },
          [&](Authenticate::Response response) {
            auto usr = user(response.user_id);
            usr.loadAvatar(std::move(response.avatar));
            GetSelectedVoiceChannel getSelectedVoiceChannel{};
            webSocket.sendText(getSelectedVoiceChannel.json());
            webSocket.sendText(Subscribe{.evt = "VOICE_CHANNEL_SELECT"}.json());
          },
          [&](Subscribe::Response response) {
            std::cout << "subscribed" << std::endl;
          },
          [&](Unsubscribe::Response response) {
            std::cout << "unsubscribed" << std::endl;
          },
          [&](GetSelectedVoiceChannel::Response response) {
            if (!response.has_value()) {
              std::cout << "no voice channel selected" << std::endl;
              return;
            }
            auto &r = response.value();
            change_the_channel(r.channel_id);
            std::cout << "channel id is " << r.channel_id << std::endl;
            for (auto &user : r.users) {
              auto &usr = ::user(user.user_id);
              usr.loadAvatar(std::move(user.avatar));
              usr.channel_id = r.channel_id;
            }
          },
          [&](dispatch::Ready event) {
            std::cout << "ready" << std::endl;
            if (event.v != 1) {
              std::cerr << "unsupported version " << event.v << ", expecting 1"
                        << std::endl;
              return;
            }
            Authorize authorize{};
            webSocket.sendText(authorize.json());
          },
          [&](dispatch::VoiceStateCreate event) {
            auto &usr = user(event.user_id);
            usr.loadAvatar(std::move(event.avatar));
            usr.deafened = event.deaf;
            usr.muted = event.mute;
            usr.channel_id = *channel_id;
          },
          [&](dispatch::VoiceStateUpdate event) {
            auto &usr = user(event.user_id);
            usr.deafened = event.deaf;
            usr.muted = event.mute;
            usr.channel_id = *channel_id;
          },
          [&](dispatch::VoiceStateDelete event) {
            auto &usr = user(event.user_id);
          },
          [&](dispatch::SpeakingStart event) {
            auto user_id = event.user_id;
            auto &usr = user(user_id);
            usr.speaking = current_time;
            usr.channel_id = event.channel_id;
            std::cout << "user " << user_id << " is speaking" << std::endl;
          },
          [&](dispatch::SpeakingStop event) {
            auto user_id = event.user_id;
            auto &usr = user(user_id);
            usr.speaking = std::nullopt;
            usr.channel_id = event.channel_id;
            std::cout << "user " << user_id << " stopped speaking" << std::endl;
          },
          [&](dispatch::VoiceChannelSelect event) {
            if (event.has_value()) {
              change_the_channel(event->channel_id);
              std::cout << "channel id is " << event->channel_id << std::endl;
            } else {
              change_the_channel(std::nullopt);
              std::cout << "channel id is null" << std::endl;
            }
          });
    } else if (msg->type == ix::WebSocketMessageType::Open) {
      std::cout << "Connection established - " << msg->str << std::endl;
      std::cout << "> " << std::flush;
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      std::cout << "Connection error: " << msg->errorInfo.reason << std::endl;
      std::cout << "> " << std::flush;
    } else if (msg->type == ix::WebSocketMessageType::Close) {
      std::cout << "Connection closed - " << msg->str << std::endl;
      std::cout << "> " << std::flush;
    } else if (msg->type == ix::WebSocketMessageType::Ping) {
      std::cout << "ping" << std::endl;
      std::cout << "> " << std::flush;
    } else if (msg->type == ix::WebSocketMessageType::Pong) {
      std::cout << "pong" << std::endl;
      std::cout << "> " << std::flush;
    } else if (msg->type == ix::WebSocketMessageType::Fragment) {
      std::cout << "fragment" << std::endl;
      std::cout << "> " << std::flush;
    }
  });

  webSocket.start();
  std::cout << "> " << std::flush;
}

void update() {
  glClearColor(0, 0, 0, 0); // background(0, 0, 0, 0) because simple-2d doesnt
                            // support alpha backgrounds yet
  glClear(GL_COLOR_BUFFER_BIT);
  int x = 0;
  for (auto &[id, user] : all_users) {
    if (user.channel_id != discord::channel_id)
      continue;
    if (user.image.width == 0)
      continue;
    color(1, 1, 1, 1);
    int y = 0;
    if (user.speaking.has_value()) {
      y = 25 * std::abs(std::sin(3 * (current_time - *user.speaking)));
    }
    image(user.image, x, y, 100, 100);
    if (!user.speaking.has_value()) {
      color(0, 0, 0, 0.4);
      rect(x, 0, 100, 100);
    }
    x += 100;
  }
}
