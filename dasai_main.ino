#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <vector>
#include <functional>
#include <memory>
#include <DFRobotDFPlayerMini.h> // Thêm thư viện DFPlayer Mini

// ===== CONFIG =====
// Các giá trị cấu hình có thể được thay đổi dễ dàng
namespace Config {
  // Hiển thị
  constexpr uint8_t DISPLAY_ROTATION = 3;
  constexpr uint8_t FRAME_DELAY_MS = 20;
  constexpr uint16_t VIDEO_DELAY_MS = 300;
  
  // GPIO và cảm biến
  constexpr uint8_t BUTTON_PIN = 13;
  constexpr uint8_t ADC_PIN = 12;
  constexpr uint16_t ADC_THRESHOLD = 2500;
  constexpr uint8_t DEBOUNCE_TIME_MS = 50;
  constexpr uint16_t HOLDING_TIME_MS = 200;

  // Chỉ số video đặc biệt
  constexpr uint8_t SLEEP_VIDEO_INDEX = 0;  // video01
  constexpr uint8_t SPECIAL_VIDEO_INDEX = 7; // video08
  
  // Audio
  constexpr uint8_t AUDIO_VOLUME = 20; // Âm lượng (0-30)
}

// ===== CÁC KIỂU DỮ LIỆU =====
typedef struct _VideoInfo {
  const uint8_t* const* frames;
  const uint16_t* frames_size;
  uint16_t num_frames;
  uint8_t audio_idx;  // Thêm trường audio_idx
} VideoInfo;

// State machine cho player
enum class PlayerState {
  NORMAL_PLAYBACK,
  PLAY_SPECIAL_VIDEO,
  PLAY_SLEEP_VIDEO,
  SKIP_CURRENT,
  LOOP_CURRENT    // Thêm trạng thái lặp video hiện tại
};

// Định nghĩa các sự kiện đầu vào
enum class InputEvent {
  BUTTON_PRESS,        // Nhấn nút (nhấn nhẹ)
  BUTTON_HOLD_START,   // Bắt đầu giữ nút
  BUTTON_HOLD_END,     // Kết thúc giữ nút
  BUTTON_DOUBLE_CLICK, // Nhấn đúp (có thể thêm sau)
  ADC_HIGH,            // ADC vượt ngưỡng
  ADC_LOW,             // ADC dưới ngưỡng
  CUSTOM_EVENT         // Dành cho mở rộng sau này
};

// Cấu trúc để theo dõi các input
struct InputState {
  bool buttonPressed = false;
  bool buttonHeld = false;
  unsigned long buttonPressTime = 0;
  unsigned long lastClickTime = 0;
  int lastButtonState = HIGH;
  unsigned long lastDebounceTime = 0;
  bool adcHighValue = false;
};

// ===== KHAI BÁO VIDEOS =====
// Lưu ý: Bạn cần cập nhật định nghĩa VideoInfo trong các file video
// để bao gồm audio_idx hoặc sử dụng phương pháp ánh xạ đã đề cập
#include "video01.h"
#include "video02.h"
#include "video03.h"
#include "video04.h"
#include "video05.h"
#include "video06.h"
#include "video07.h"
#include "video08.h"
#include "video09.h"
#include "video10.h"
#include "video11.h"
#include "video12.h"
#include "video13.h"
#include "video14.h"

// ===== QUẢN LÝ HIỂN THỊ =====
class DisplayManager {
private:
  TFT_eSPI _tft;
  
public:
  DisplayManager() : _tft() {}
  
  void init() {
    _tft.begin();
    _tft.setRotation(Config::DISPLAY_ROTATION);
    _tft.fillScreen(TFT_BLACK);
    
    // Cấu hình JPEG decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tftOutputCallback);
  }
  
  TFT_eSPI* getTft() { return &_tft; }
  
  static bool tftOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    TFT_eSPI* tft = getInstance()._tft;
    if (x >= tft->width() || y >= tft->height()) return false;
    tft->pushImage(x, y, w, h, bitmap);
    return true;
  }
  
  void drawFrame(const VideoInfo* video, uint16_t frameIndex) {
    const uint8_t* jpg_data = (const uint8_t*)pgm_read_ptr(&video->frames[frameIndex]);
    uint16_t jpg_size = pgm_read_word(&video->frames_size[frameIndex]);
  
    if (TJpgDec.drawJpg(0, 0, jpg_data, jpg_size)) {
      Serial.printf("Decode failed on frame %d\n", frameIndex);
    }
  }
  
  void clear() {
    _tft.fillScreen(TFT_BLACK);
  }
  
  static DisplayManager& getInstance() {
    static DisplayManager instance;
    return instance;
  }
};

// ===== QUẢN LÝ AUDIO =====
class AudioManager {
private:
  DFRobotDFPlayerMini _dfPlayer;
  bool _isPlaying = false;
  uint8_t _currentTrack = 0;
  unsigned long _lastCommandTime = 0;
  static constexpr unsigned long MIN_COMMAND_INTERVAL = 50; // Thời gian tối thiểu giữa các lệnh gửi đến DFPlayer (ms)

public:
  AudioManager() {}
  
  void init() {
    Serial2.begin(9600); // Khởi tạo Serial2 với baud rate 9600 (chuẩn cho DFPlayer)
    
    Serial.println("Initializing DFPlayer...");
    if (!_dfPlayer.begin(Serial2)) {
      Serial.println("Failed to initialize DFPlayer!");
      Serial.println("Please check the connection and SD card.");
    } else {
      Serial.println("DFPlayer Mini initialized!");
      _dfPlayer.setTimeOut(500); // Set timeout 500ms
      _dfPlayer.volume(Config::AUDIO_VOLUME); // Set volume
      _dfPlayer.EQ(DFPLAYER_EQ_NORMAL); // Set EQ
    }
  }
  
  void play(uint8_t trackNumber) {
    if (_currentTrack == trackNumber && _isPlaying) {
      return; // Đã đang phát bản nhạc này rồi
    }
    
    if (millis() - _lastCommandTime < MIN_COMMAND_INTERVAL) {
      delay(MIN_COMMAND_INTERVAL); // Đảm bảo khoảng cách giữa các lệnh
    }
    
    _dfPlayer.play(trackNumber);
    _currentTrack = trackNumber;
    _isPlaying = true;
    _lastCommandTime = millis();
    
    Serial.printf("Playing audio track: %d\n", trackNumber);
  }
  
  void stop() {
    if (!_isPlaying) {
      return; // Không đang phát, không cần dừng
    }
    
    if (millis() - _lastCommandTime < MIN_COMMAND_INTERVAL) {
      delay(MIN_COMMAND_INTERVAL);
    }
    
    _dfPlayer.stop();
    _isPlaying = false;
    _lastCommandTime = millis();
    
    Serial.println("Audio playback stopped");
  }
  
  void setVolume(uint8_t volume) {
    if (millis() - _lastCommandTime < MIN_COMMAND_INTERVAL) {
      delay(MIN_COMMAND_INTERVAL);
    }
    
    _dfPlayer.volume(volume);
    _lastCommandTime = millis();
  }
  
  bool isPlaying() const {
    return _isPlaying;
  }
  
  // Singleton pattern
  static AudioManager& getInstance() {
    static AudioManager instance;
    return instance;
  }
};

// ===== QUẢN LÝ VIDEO =====
class VideoManager {
private:
  std::vector<VideoInfo*> _videoList;
  uint8_t _currentVideoIndex = 0;
  uint16_t _currentFrameIndex = 0;
  PlayerState _state = PlayerState::NORMAL_PLAYBACK;
  bool _loopCurrentVideo = false;
  
public:
  VideoManager() {
    _videoList = {
      &video01, &video02, &video03, &video04, &video05, &video06, &video07,
      &video08, &video09, &video10, &video11, &video12, &video13, &video14
    };
  }
  
  void update(PlayerState newState) {
    PlayerState oldState = _state;
    _state = newState;
    
    // Xử lý các trạng thái đặc biệt
    switch (newState) {
      case PlayerState::SKIP_CURRENT:
        stopAudioIfNeeded();
        nextVideo();
        _state = PlayerState::NORMAL_PLAYBACK;
        break;
        
      case PlayerState::LOOP_CURRENT:
        _loopCurrentVideo = true;
        playAudioForCurrentVideo();
        break;
        
      case PlayerState::PLAY_SLEEP_VIDEO:
        _currentVideoIndex = Config::SLEEP_VIDEO_INDEX;
        _currentFrameIndex = 0;
        playAudioForCurrentVideo();
        break;
        
      case PlayerState::PLAY_SPECIAL_VIDEO:
        _currentVideoIndex = Config::SPECIAL_VIDEO_INDEX;
        _currentFrameIndex = 0;
        playAudioForCurrentVideo();
        break;
        
      case PlayerState::NORMAL_PLAYBACK:
        _loopCurrentVideo = false;
        stopAudioIfNeeded();
        
        // Nếu đang xem video đặc biệt, chuyển sang video thông thường tiếp theo
        if (_currentVideoIndex == Config::SLEEP_VIDEO_INDEX || 
            _currentVideoIndex == Config::SPECIAL_VIDEO_INDEX) {
          nextVideo();
        }
        break;
    }
  }
  
  void playAudioForCurrentVideo() {
    // Phát audio nếu đang ở các trạng thái cho phép
    if (_state == PlayerState::LOOP_CURRENT ||
        _state == PlayerState::PLAY_SLEEP_VIDEO ||
        _state == PlayerState::PLAY_SPECIAL_VIDEO) {
      uint8_t audioIdx = getCurrentVideo()->audio_idx;
      AudioManager::getInstance().play(audioIdx);
    }
  }
  
  void stopAudioIfNeeded() {
    // Dừng audio khi trở về chế độ phát thông thường
    if (_state == PlayerState::NORMAL_PLAYBACK) {
      AudioManager::getInstance().stop();
    }
  }
  
  void nextFrame() {
    _currentFrameIndex++;
    if (_currentFrameIndex >= getCurrentVideo()->num_frames) {
      _currentFrameIndex = 0;
      
      // Chỉ chuyển video nếu không trong chế độ lặp và trạng thái là normal
      if (!_loopCurrentVideo && _state == PlayerState::NORMAL_PLAYBACK) {
        stopAudioIfNeeded();
        nextVideo();
      }
    }
  }
  
  void nextVideo() {
    _currentFrameIndex = 0;
    
    if (_state == PlayerState::NORMAL_PLAYBACK) {
      do {
        _currentVideoIndex = (_currentVideoIndex + 1) % _videoList.size();
        // Trong chế độ thường, bỏ qua video đặc biệt
      } while (_state == PlayerState::NORMAL_PLAYBACK && 
              (_currentVideoIndex == Config::SLEEP_VIDEO_INDEX || 
               _currentVideoIndex == Config::SPECIAL_VIDEO_INDEX));
      
      stopAudioIfNeeded();
      
    } else if (_state == PlayerState::PLAY_SLEEP_VIDEO) {
      _currentVideoIndex = Config::SLEEP_VIDEO_INDEX;
      playAudioForCurrentVideo();
      
    } else if (_state == PlayerState::PLAY_SPECIAL_VIDEO) {
      _currentVideoIndex = Config::SPECIAL_VIDEO_INDEX;
      playAudioForCurrentVideo();
    }
  }
  
  VideoInfo* getCurrentVideo() const {
    return _videoList[_currentVideoIndex];
  }
  
  uint16_t getCurrentFrameIndex() const {
    return _currentFrameIndex;
  }
  
  uint8_t getCurrentVideoIndex() const {
    return _currentVideoIndex;
  }
  
  PlayerState getState() const {
    return _state;
  }
  
  bool isLastFrame() const {
    return _currentFrameIndex == getCurrentVideo()->num_frames - 1;
  }
  
  bool isLoopingCurrentVideo() const {
    return _loopCurrentVideo;
  }
  
  static VideoManager& getInstance() {
    static VideoManager instance;
    return instance;
  }
};

// ===== INPUT SCENARIO INTERFACE =====
class InputScenario {
public:
  virtual ~InputScenario() = default;
  
  // Xử lý sự kiện đầu vào và trả về trạng thái mới nếu cần thay đổi
  virtual PlayerState handleEvent(InputEvent event, const InputState& state) = 0;
  
  // Kiểm tra xem kịch bản có áp dụng được trong trạng thái hiện tại không
  virtual bool isActive(const InputState& state) const { return true; }
  
  // Cho phép kịch bản tự cập nhật trạng thái nội bộ
  virtual void update() {}
};

// ===== SLIDE SCENARIO =====
// Kịch bản mặc định: Nhấn nút để chuyển video, giữ nút để phát video đặc biệt
class SlideScenario : public InputScenario {
public:
  PlayerState handleEvent(InputEvent event, const InputState& state) override {
    switch (event) {
      case InputEvent::BUTTON_PRESS:
        return PlayerState::SKIP_CURRENT;
        
      case InputEvent::BUTTON_HOLD_START:
        return PlayerState::PLAY_SPECIAL_VIDEO;
        
      case InputEvent::BUTTON_HOLD_END:
        return PlayerState::NORMAL_PLAYBACK;
        
      default:
        return PlayerState::NORMAL_PLAYBACK;
    }
  }
};

// ===== SLEEPING SCENARIO =====
// Kịch bản ADC: ADC cao thì xem video intro
class SleepingScenario : public InputScenario {
public:
  PlayerState handleEvent(InputEvent event, const InputState& state) override {
    switch (event) {
      case InputEvent::ADC_HIGH:
        return PlayerState::PLAY_SLEEP_VIDEO;
        
      case InputEvent::ADC_LOW:
        return PlayerState::NORMAL_PLAYBACK;
        
      default:
        return PlayerState::NORMAL_PLAYBACK;
    }
  }
  
  // Kịch bản ADC không hoạt động khi đang giữ nút
  bool isActive(const InputState& state) const override {
    return !state.buttonHeld;
  }
};

// ===== LOOP SCENARIO =====
// Kịch bản nhấn đúp: Bật/tắt chế độ lặp video hiện tại
class LoopScenario : public InputScenario {
private:
  bool _loopMode = false;
  static constexpr unsigned long DOUBLE_CLICK_TIME = 300; // ms
  
public:
  PlayerState handleEvent(InputEvent event, const InputState& state) override {
    if (event == InputEvent::BUTTON_PRESS) {
      unsigned long now = millis();
      if (now - state.lastClickTime < DOUBLE_CLICK_TIME) {
        // Phát hiện nhấn đúp, chuyển đổi chế độ lặp
        _loopMode = !_loopMode;
        return _loopMode ? PlayerState::LOOP_CURRENT : PlayerState::NORMAL_PLAYBACK;
      }
    }
    return PlayerState::NORMAL_PLAYBACK;
  }
};

// ===== QUẢN LÝ INPUT =====
// Lớp Observer để nhận các thông báo từ InputManager
class InputObserver {
public:
  virtual ~InputObserver() = default;
  virtual void onInputEvent(InputEvent event, const InputState& state) = 0;
};

class InputManager {
private:
  InputState _state;
  std::vector<std::shared_ptr<InputScenario>> _scenarios;
  std::vector<std::reference_wrapper<InputObserver>> _observers;
  std::function<void(PlayerState)> _stateChangeCallback;
  
public:
  InputManager() {
    // Đăng ký các kịch bản mặc định
    _scenarios.push_back(std::make_shared<LoopScenario>());
    _scenarios.push_back(std::make_shared<SleepingScenario>());
    _scenarios.push_back(std::make_shared<SlideScenario>());
  }
  
  void init() {
    pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
  }
  
  // Thêm kịch bản mới
  void addScenario(std::shared_ptr<InputScenario> scenario) {
    _scenarios.push_back(scenario);
  }
  
  // Thêm observer
  void addObserver(InputObserver& observer) {
    _observers.push_back(std::ref(observer));
  }
  
  void setStateChangeCallback(std::function<void(PlayerState)> callback) {
    _stateChangeCallback = callback;
  }
  
  void update() {
    readButton();
    readADC();
    
    // Cho phép các kịch bản tự cập nhật
    for (auto& scenario : _scenarios) {
      scenario->update();
    }
  }
  
  const InputState& getState() const {
    return _state;
  }
  
private:
  // Xử lý sự kiện đầu vào và thông báo cho các kịch bản
  void handleInputEvent(InputEvent event) {
    // Thông báo cho tất cả observers
    for (auto& observer : _observers) {
      observer.get().onInputEvent(event, _state);
    }
    
    // Xử lý các kịch bản
    if (!_stateChangeCallback) return;
    
    for (auto& scenario : _scenarios) {
      if (scenario->isActive(_state)) {
        PlayerState newState = scenario->handleEvent(event, _state);
        
        // Chỉ gửi thay đổi trạng thái nếu khác với trạng thái mặc định
        if (newState != PlayerState::NORMAL_PLAYBACK || event == InputEvent::BUTTON_HOLD_END) {
          _stateChangeCallback(newState);
          break; // Chỉ xử lý kịch bản đầu tiên khớp
        }
      }
    }
  }
  
  void readButton() {
    int reading = digitalRead(Config::BUTTON_PIN);
    
    // Xử lý chống dội
    if (reading != _state.lastButtonState) {
      _state.lastDebounceTime = millis();
    }
    
    if ((millis() - _state.lastDebounceTime) > Config::DEBOUNCE_TIME_MS) {
      // Nếu nút được nhấn (LOW)
      if (reading == LOW && _state.lastButtonState == HIGH) {
        _state.buttonPressed = true;
        _state.buttonPressTime = millis();
      } 
      // Nếu nút được thả ra (HIGH)
      else if (reading == HIGH && _state.lastButtonState == LOW) {
        _state.buttonPressed = false;
        
        // Nếu đã giữ nút
        if (_state.buttonHeld) {
          _state.buttonHeld = false;
          handleInputEvent(InputEvent::BUTTON_HOLD_END);
        }
        // Nếu chỉ nhấn nhẹ
        else if ((millis() - _state.buttonPressTime) < Config::HOLDING_TIME_MS) {
          // Lưu thời gian nhấn để phát hiện double-click
          unsigned long prevClickTime = _state.lastClickTime;
          _state.lastClickTime = millis();
          
          handleInputEvent(InputEvent::BUTTON_PRESS);
        }
      }
      
      // Kiểm tra nếu đang giữ nút
      if (_state.buttonPressed && !_state.buttonHeld && 
          (millis() - _state.buttonPressTime > Config::HOLDING_TIME_MS)) {
        _state.buttonHeld = true;
        handleInputEvent(InputEvent::BUTTON_HOLD_START);
      }
    }
    
    _state.lastButtonState = reading;
  }
  
  void readADC() {
    int adcValue = analogRead(Config::ADC_PIN);
    bool prevState = _state.adcHighValue;
    
    _state.adcHighValue = (adcValue > Config::ADC_THRESHOLD);
    
    // Nếu có thay đổi trạng thái
    if (_state.adcHighValue != prevState) {
      if (_state.adcHighValue) {
        handleInputEvent(InputEvent::ADC_HIGH);
      } else {
        handleInputEvent(InputEvent::ADC_LOW);
      }
    }
  }
  
public:
  static InputManager& getInstance() {
    static InputManager instance;
    return instance;
  }
};

// ===== QUẢN LÝ TASK =====
// Class quản lý các tác vụ định kỳ và xử lý bất đồng bộ
class TaskManager {
private:
  unsigned long _lastFrameTime = 0;
  unsigned long _lastVideoTime = 0;
  bool _waitingForVideoDelay = false;
  
public:
  void update() {
    auto& videoMgr = VideoManager::getInstance();
    auto& displayMgr = DisplayManager::getInstance();
    
    unsigned long currentTime = millis();
    
    // Xử lý delay giữa các video
    if (_waitingForVideoDelay) {
      if (currentTime - _lastVideoTime >= Config::VIDEO_DELAY_MS) {
        _waitingForVideoDelay = false;
        videoMgr.nextVideo();
      } else {
        return; // Đang trong thời gian delay, không làm gì
      }
    }
    
    // Xử lý thời gian hiển thị frame
    if (currentTime - _lastFrameTime >= Config::FRAME_DELAY_MS) {
      _lastFrameTime = currentTime;
      
      // Hiển thị frame hiện tại
      displayMgr.drawFrame(videoMgr.getCurrentVideo(), videoMgr.getCurrentFrameIndex());
      
      // Nếu đây là frame cuối và đang trong chế độ thông thường và không lặp
      if (videoMgr.isLastFrame() && 
          videoMgr.getState() == PlayerState::NORMAL_PLAYBACK && 
          !videoMgr.isLoopingCurrentVideo()) {
        _lastVideoTime = currentTime;
        _waitingForVideoDelay = true;
      } else {
        videoMgr.nextFrame();
      }
    }
  }
  
  static TaskManager& getInstance() {
    static TaskManager instance;
    return instance;
  }
};

// ===== LOGGER OBSERVER EXAMPLE =====
// Ví dụ về một observer để ghi log các sự kiện đầu vào
class InputLogger : public InputObserver {
public:
  void onInputEvent(InputEvent event, const InputState& state) override {
    // In debug log về sự kiện
    String eventName;
    
    switch (event) {
      case InputEvent::BUTTON_PRESS: eventName = "BUTTON_PRESS"; break;
      case InputEvent::BUTTON_HOLD_START: eventName = "BUTTON_HOLD_START"; break;
      case InputEvent::BUTTON_HOLD_END: eventName = "BUTTON_HOLD_END"; break;
      case InputEvent::BUTTON_DOUBLE_CLICK: eventName = "BUTTON_DOUBLE_CLICK"; break;
      case InputEvent::ADC_HIGH: eventName = "ADC_HIGH"; break;
      case InputEvent::ADC_LOW: eventName = "ADC_LOW"; break;
      default: eventName = "UNKNOWN_EVENT"; break;
    }
    
    Serial.printf("Input Event: %s, ButtonHeld: %d, ADC: %d\n",
                 eventName.c_str(), state.buttonHeld, state.adcHighValue);
  }
};

// ===== CHƯƠNG TRÌNH CHÍNH =====
InputLogger logger; // Observer logger toàn cục

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing Video Player...");
  
  // Khởi tạo các thành phần
  DisplayManager::getInstance().init();
  InputManager::getInstance().init();
  AudioManager::getInstance().init(); // Khởi tạo AudioManager
  
  // Thêm logger để theo dõi các sự kiện
  InputManager::getInstance().addObserver(logger);
  
  // Cấu hình callback khi trạng thái thay đổi
  InputManager::getInstance().setStateChangeCallback([](PlayerState newState) {
    VideoManager::getInstance().update(newState);
  });
  
  Serial.println("Initialization complete!");
}

void loop() {
  // Cập nhật đầu vào
  InputManager::getInstance().update();
  
  // Xử lý hiển thị và chuyển frame
  TaskManager::getInstance().update();
}
