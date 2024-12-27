#include "fujitsu_legacy.h"

namespace esphome {
namespace fujitsu_legacy {

// bytes' bits are reversed for fujitsu, so nibbles are ordered 1, 0, 3, 2, 5, 4, etc...

#define SET_NIBBLE(message, nibble, value) \
  ((message)[(nibble) / 2] |= ((value) &0b00001111) << (((nibble) % 2) ? 0 : 4))
#define GET_NIBBLE(message, nibble) (((message)[(nibble) / 2] >> (((nibble) % 2) ? 0 : 4)) & 0b00001111)

static const char *const TAG = "fujitsu_legacy.climate";

// Common header
const uint8_t FUJITSU_LEGACY_COMMON_LENGTH = 6;
const uint8_t FUJITSU_LEGACY_COMMON_BYTE0 = 0x14;
const uint8_t FUJITSU_LEGACY_COMMON_BYTE1 = 0x63;
const uint8_t FUJITSU_LEGACY_COMMON_BYTE2 = 0x00;
const uint8_t FUJITSU_LEGACY_COMMON_BYTE3 = 0x10;
const uint8_t FUJITSU_LEGACY_COMMON_BYTE4 = 0x10;
const uint8_t FUJITSU_LEGACY_MESSAGE_TYPE_BYTE = 5;
const uint8_t FUJITSU_LEGACY_COMMON_MARKER[] =
 {
  FUJITSU_LEGACY_COMMON_BYTE0,
  FUJITSU_LEGACY_COMMON_BYTE1,
  FUJITSU_LEGACY_COMMON_BYTE2,
  FUJITSU_LEGACY_COMMON_BYTE3,
  FUJITSU_LEGACY_COMMON_BYTE4,
 };

// State message - temp & fan etc.
const uint8_t FUJITSU_LEGACY_STATE_MESSAGE_LENGTH = 15;
const uint8_t FUJITSU_LEGACY_MESSAGE_TYPE_STATE = 0xFC;

// Util messages - off & swing etc.
const uint8_t FUJITSU_LEGACY_UTIL_MESSAGE_LENGTH = 6;
const uint8_t FUJITSU_LEGACY_MESSAGE_TYPE_OFF = 0x02;
const uint8_t FUJITSU_LEGACY_MESSAGE_TYPE_AIRFLOW_DIRECTION = 0x6C;
const uint8_t FUJITSU_LEGACY_MESSAGE_TYPE_SWING_LOUVER = 0x6D;

// State header
const uint8_t FUJITSU_LEGACY_STATE_HEADER_BYTE0 = 0x08;
const uint8_t FUJITSU_LEGACY_STATE_HEADER_BYTE1 = 0x30;

// Temperature
const uint8_t FUJITSU_LEGACY_TEMPERATURE_NIBBLE = 16;

// Power on
const uint8_t FUJITSU_LEGACY_POWER_ON_NIBBLE = 17;
const uint8_t FUJITSU_LEGACY_POWER_OFF = 0x00;
const uint8_t FUJITSU_LEGACY_POWER_ON = 0x01;

// Mode
const uint8_t FUJITSU_LEGACY_MODE_NIBBLE = 19;
const uint8_t FUJITSU_LEGACY_MODE_AUTO = 0x00;
const uint8_t FUJITSU_LEGACY_MODE_COOL = 0x01;
const uint8_t FUJITSU_LEGACY_MODE_DRY = 0x02;
const uint8_t FUJITSU_LEGACY_MODE_FAN = 0x03;
const uint8_t FUJITSU_LEGACY_MODE_HEAT = 0x04;

// Swing - separate command for AR-DB1

// Fan
const uint8_t FUJITSU_LEGACY_FAN_NIBBLE = 21;
const uint8_t FUJITSU_LEGACY_FAN_AUTO = 0x00;
const uint8_t FUJITSU_LEGACY_FAN_HIGH = 0x01;
const uint8_t FUJITSU_LEGACY_FAN_MEDIUM = 0x02;
const uint8_t FUJITSU_LEGACY_FAN_LOW = 0x03;
const uint8_t FUJITSU_LEGACY_FAN_QUIET = 0x04;

const uint16_t FUJITSU_LEGACY_HEADER_MARK = 3300;
const uint16_t FUJITSU_LEGACY_HEADER_SPACE = 1600;

const uint16_t FUJITSU_LEGACY_BIT_MARK = 420;
const uint16_t FUJITSU_LEGACY_ONE_SPACE = 1200;
const uint16_t FUJITSU_LEGACY_ZERO_SPACE = 420;

const uint16_t FUJITSU_LEGACY_TRL_MARK = 420;
const uint16_t FUJITSU_LEGACY_TRL_SPACE = 8000;

const uint32_t FUJITSU_LEGACY_CARRIER_FREQUENCY = 38000;

void FujitsuLegacyClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->fan_mode = *call.get_fan_mode();
  if (this->power_ && call.get_swing_mode().has_value()) {
    esphome::climate::ClimateSwingMode new_mode = *call.get_swing_mode();
    if (this->swing_mode != new_mode) {
      this->swing_mode = new_mode;
      this->transmit_swing_();
    }
  }
  if (call.get_preset().has_value())
    this->preset = *call.get_preset();
  this->transmit_state();
  this->publish_state();
}

void FujitsuLegacyClimate::transmit_state() {
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    this->transmit_off_();
    return;
  }

  ESP_LOGV(TAG, "Transmit state");

  uint8_t remote_state[FUJITSU_LEGACY_STATE_MESSAGE_LENGTH] = {
    // Common message header
    FUJITSU_LEGACY_COMMON_BYTE0,
    FUJITSU_LEGACY_COMMON_BYTE1,
    FUJITSU_LEGACY_COMMON_BYTE2,
    FUJITSU_LEGACY_COMMON_BYTE3,
    FUJITSU_LEGACY_COMMON_BYTE4,
    FUJITSU_LEGACY_MESSAGE_TYPE_STATE,
    FUJITSU_LEGACY_STATE_HEADER_BYTE0,
    FUJITSU_LEGACY_STATE_HEADER_BYTE1};

  // Set temperature
  uint8_t temperature_clamped =
      (uint8_t) roundf(clamp<float>(this->target_temperature, FUJITSU_LEGACY_TEMP_MIN, FUJITSU_LEGACY_TEMP_MAX));
  uint8_t temperature_offset = temperature_clamped - FUJITSU_LEGACY_TEMP_MIN;
  SET_NIBBLE(remote_state, FUJITSU_LEGACY_TEMPERATURE_NIBBLE, temperature_offset);

  // Set power on
  if (!this->power_) {
    SET_NIBBLE(remote_state, FUJITSU_LEGACY_POWER_ON_NIBBLE, FUJITSU_LEGACY_POWER_ON);
  }

  // Set mode
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_MODE_NIBBLE, FUJITSU_LEGACY_MODE_COOL);
      break;
    case climate::CLIMATE_MODE_HEAT:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_MODE_NIBBLE, FUJITSU_LEGACY_MODE_HEAT);
      break;
    case climate::CLIMATE_MODE_DRY:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_MODE_NIBBLE, FUJITSU_LEGACY_MODE_DRY);
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_MODE_NIBBLE, FUJITSU_LEGACY_MODE_FAN);
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
    default:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_MODE_NIBBLE, FUJITSU_LEGACY_MODE_AUTO);
      break;
  }

  // Set fan
  switch (this->fan_mode.value()) {
    case climate::CLIMATE_FAN_HIGH:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_FAN_NIBBLE, FUJITSU_LEGACY_FAN_HIGH);
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_FAN_NIBBLE, FUJITSU_LEGACY_FAN_MEDIUM);
      break;
    case climate::CLIMATE_FAN_LOW:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_FAN_NIBBLE, FUJITSU_LEGACY_FAN_LOW);
      break;
    case climate::CLIMATE_FAN_QUIET:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_FAN_NIBBLE, FUJITSU_LEGACY_FAN_QUIET);
      break;
    case climate::CLIMATE_FAN_AUTO:
    default:
      SET_NIBBLE(remote_state, FUJITSU_LEGACY_FAN_NIBBLE, FUJITSU_LEGACY_FAN_AUTO);
      break;
  }

  remote_state[FUJITSU_LEGACY_STATE_MESSAGE_LENGTH - 1] = this->checksum_state_(remote_state);

  this->transmit_(remote_state, FUJITSU_LEGACY_STATE_MESSAGE_LENGTH);

  this->power_ = true;
}

void FujitsuLegacyClimate::transmit_cmd_(uint8_t cmd) {
  uint8_t remote_state[FUJITSU_LEGACY_UTIL_MESSAGE_LENGTH] = {
    // Common message header
    FUJITSU_LEGACY_COMMON_BYTE0,
    FUJITSU_LEGACY_COMMON_BYTE1,
    FUJITSU_LEGACY_COMMON_BYTE2,
    FUJITSU_LEGACY_COMMON_BYTE3,
    FUJITSU_LEGACY_COMMON_BYTE4,
    cmd};

  this->transmit_(remote_state, FUJITSU_LEGACY_UTIL_MESSAGE_LENGTH);
}

void FujitsuLegacyClimate::transmit_off_() {
  ESP_LOGV(TAG, "Transmit off");
  this->transmit_cmd_(FUJITSU_LEGACY_MESSAGE_TYPE_OFF);
  this->power_ = false;
  this->swing_mode = climate::CLIMATE_SWING_OFF;
}

void FujitsuLegacyClimate::transmit_swing_() {
  ESP_LOGV(TAG, "Transmit swing louver");
  this->transmit_cmd_(FUJITSU_LEGACY_MESSAGE_TYPE_SWING_LOUVER);
}

void FujitsuLegacyClimate::transmit_(uint8_t const *message, uint8_t length) {
  ESP_LOGV(TAG, "Transmit message length %d", length);

  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();

  data->set_carrier_frequency(FUJITSU_LEGACY_CARRIER_FREQUENCY);

  // Header
  data->mark(FUJITSU_LEGACY_HEADER_MARK);
  data->space(FUJITSU_LEGACY_HEADER_SPACE);

  // Data
  for (uint8_t i = 0; i < length; ++i) {
    const uint8_t byte = message[i];
    for (uint8_t mask = 0b00000001; mask > 0; mask <<= 1) {  // write from right to left
      data->mark(FUJITSU_LEGACY_BIT_MARK);
      bool bit = byte & mask;
      data->space(bit ? FUJITSU_LEGACY_ONE_SPACE : FUJITSU_LEGACY_ZERO_SPACE);
    }
  }

  // Footer
  data->mark(FUJITSU_LEGACY_TRL_MARK);
  data->space(FUJITSU_LEGACY_TRL_SPACE);

  transmit.perform();
}

uint8_t FujitsuLegacyClimate::checksum_state_(uint8_t const *message) {
  uint8_t checksum = 0;
  for (uint8_t i = 7; i < FUJITSU_LEGACY_STATE_MESSAGE_LENGTH - 1; ++i) {
    checksum += message[i];
  }
  return 256 - checksum;
}

bool FujitsuLegacyClimate::on_receive(remote_base::RemoteReceiveData data) {
  ESP_LOGV(TAG, "Received IR message");

  // Validate header
  if (!data.expect_item(FUJITSU_LEGACY_HEADER_MARK, FUJITSU_LEGACY_HEADER_SPACE)) {
    ESP_LOGV(TAG, "Header fail");
    return false;
  }

  uint8_t recv_message[FUJITSU_LEGACY_STATE_MESSAGE_LENGTH] = {0};

  // Read header
  for (uint8_t byte = 0; byte < FUJITSU_LEGACY_COMMON_LENGTH; ++byte) {
    // Read bit
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (data.expect_item(FUJITSU_LEGACY_BIT_MARK, FUJITSU_LEGACY_ONE_SPACE)) {
        recv_message[byte] |= 1 << bit;  // read from right to left
      } else if (!data.expect_item(FUJITSU_LEGACY_BIT_MARK, FUJITSU_LEGACY_ZERO_SPACE)) {
        ESP_LOGV(TAG, "Byte %d bit %d fail", byte, bit);
        return false;
      }
    }
  }

  if (memcmp(recv_message, FUJITSU_LEGACY_COMMON_MARKER, sizeof(FUJITSU_LEGACY_COMMON_MARKER))) {
    ESP_LOGV(TAG, "Marker fail");
    return false;
  }

  const uint8_t recv_message_type = recv_message[FUJITSU_LEGACY_MESSAGE_TYPE_BYTE];
  uint8_t recv_message_length;

  switch (recv_message_type) {
    case FUJITSU_LEGACY_MESSAGE_TYPE_STATE:
      ESP_LOGV(TAG, "Received state message");
      recv_message_length = FUJITSU_LEGACY_STATE_MESSAGE_LENGTH;
      break;
    case FUJITSU_LEGACY_MESSAGE_TYPE_OFF:
    case FUJITSU_LEGACY_MESSAGE_TYPE_AIRFLOW_DIRECTION:
    case FUJITSU_LEGACY_MESSAGE_TYPE_SWING_LOUVER:
      ESP_LOGV(TAG, "Received util message");
      recv_message_length = FUJITSU_LEGACY_UTIL_MESSAGE_LENGTH;
      break;
    default:
      ESP_LOGV(TAG, "Unknown message type %X", recv_message_type);
      return false;
  }

  // Read message body
  for (uint8_t byte = FUJITSU_LEGACY_COMMON_LENGTH; byte < recv_message_length; ++byte) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (data.expect_item(FUJITSU_LEGACY_BIT_MARK, FUJITSU_LEGACY_ONE_SPACE)) {
        recv_message[byte] |= 1 << bit;  // read from right to left
      } else if (!data.expect_item(FUJITSU_LEGACY_BIT_MARK, FUJITSU_LEGACY_ZERO_SPACE)) {
        ESP_LOGV(TAG, "Byte %d bit %d fail", byte, bit);
        return false;
      }
    }
  }

  for (uint8_t byte = 0; byte < recv_message_length; ++byte) {
    ESP_LOGVV(TAG, "%02X", recv_message[byte]);
  }

  if (recv_message_type == FUJITSU_LEGACY_MESSAGE_TYPE_STATE) {
    const uint8_t recv_checksum = recv_message[recv_message_length - 1];
    uint8_t calculated_checksum = this->checksum_state_(recv_message);

    if (recv_checksum != calculated_checksum) {
      ESP_LOGV(TAG, "Checksum fail - expected %X - got %X", calculated_checksum, recv_checksum);
      return false;
    }
    const uint8_t recv_tempertature = GET_NIBBLE(recv_message, FUJITSU_LEGACY_TEMPERATURE_NIBBLE);
    const uint8_t offset_temperature = recv_tempertature + FUJITSU_LEGACY_TEMP_MIN;

    this->target_temperature = offset_temperature;
    ESP_LOGV(TAG, "Received temperature %d", offset_temperature);

    const uint8_t recv_mode = GET_NIBBLE(recv_message, FUJITSU_LEGACY_MODE_NIBBLE);
    ESP_LOGV(TAG, "Received mode %X", recv_mode);
    switch (recv_mode) {
      case FUJITSU_LEGACY_MODE_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case FUJITSU_LEGACY_MODE_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case FUJITSU_LEGACY_MODE_DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      case FUJITSU_LEGACY_MODE_FAN:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case FUJITSU_LEGACY_MODE_AUTO:
      default:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
    }

    const uint8_t recv_fan_mode = GET_NIBBLE(recv_message, FUJITSU_LEGACY_FAN_NIBBLE);
    ESP_LOGV(TAG, "Received fan mode %X", recv_fan_mode);
    switch (recv_fan_mode) {
      case FUJITSU_LEGACY_FAN_QUIET:
        this->fan_mode = climate::CLIMATE_FAN_QUIET;
        break;
      case FUJITSU_LEGACY_FAN_LOW:
        this->fan_mode = climate::CLIMATE_FAN_LOW;
        break;
      case FUJITSU_LEGACY_FAN_MEDIUM:
        this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        break;
      case FUJITSU_LEGACY_FAN_HIGH:
        this->fan_mode = climate::CLIMATE_FAN_HIGH;
        break;
      case FUJITSU_LEGACY_FAN_AUTO:
      default:
        this->fan_mode = climate::CLIMATE_FAN_AUTO;
        break;
    }

    this->power_ = true;
  } else if (recv_message_type == FUJITSU_LEGACY_MESSAGE_TYPE_OFF) {
    ESP_LOGV(TAG, "Received off message");
    this->mode = climate::CLIMATE_MODE_OFF;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->power_ = false;
  } else if (recv_message_type == FUJITSU_LEGACY_MESSAGE_TYPE_AIRFLOW_DIRECTION) {
    ESP_LOGV(TAG, "Received airflow direction message");
    return true;
  } else if (recv_message_type == FUJITSU_LEGACY_MESSAGE_TYPE_SWING_LOUVER) {
    ESP_LOGV(TAG, "Received swing louver message");
/* ignore - else toggles state if it sees the message sent by transmitter.
So do not track swing state set by remote.
    if (this->swing_mode == climate::CLIMATE_SWING_OFF) {
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    } else {
      this->swing_mode = climate::CLIMATE_SWING_OFF;
    }
*/
  } else {
    ESP_LOGV(TAG, "Received unsupported message type %X", recv_message_type);
    return false;
  }

  this->publish_state();
  return true;
}

}  // namespace fujitsu_legacy
}  // namespace esphome
