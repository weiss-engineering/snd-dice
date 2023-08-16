#ifndef SOUND_DICE_AVC_H_INCLUDED
#define SOUND_DICE_AVC_H_INCLUDED

/* Command types: */
#define AVC_CTYPE_CONTROL                   0x00
#define AVC_CTYPE_STATUS                    0x01
#define AVC_CTYPE_SPECIFIC_INQUIRY          0x02
#define AVC_CTYPE_NOTIFY                    0x03
#define AVC_CTYPE_GENERAL_INQUIRY           0x04

/* Response types: */
#define AVC_RESPONSE_ACCEPTED               0x09
#define AVC_RESPONSE_REJECTED               0x0A
#define AVC_RESPONSE_IN_TRANSITION          0x0B
#define AVC_RESPONSE_IMPLEMENTED            0x0C
#define AVC_RESPONSE_STABLE                 0x0C
#define AVC_RESPONSE_CHANGED                0x0D
#define AVC_RESPONSE_INTERIM                0x0F

/* Subunit types: */
#define AVC_SU_TYPE_MONITOR                 0x00
#define AVC_SU_TYPE_AUDIO                   0x01
#define AVC_SU_TYPE_PRINTER                 0x02
#define AVC_SU_TYPE_DISC                    0x03    // recorder/player
#define AVC_SU_TYPE_TAPE                    0x04    // recorder/player
#define AVC_SU_TYPE_TUNER                   0x05
#define AVC_SU_TYPE_CA                      0x06
#define AVC_SU_TYPE_CAMERA                  0x07
//#define AVC_SU_TYPE_RESERVED1             0x08
#define AVC_SU_TYPE_PANEL                   0x09
#define AVC_SU_TYPE_BULLETIN_BOARD          0x0A
#define AVC_SU_TYPE_CAMERA_STORAGE          0x0B
#define AVC_SU_TYPE_MUSIC                   0x0C
#define AVC_SU_TYPE_VENDOR_UNIQUE           0x1C
//#define AVC_SU_TYPE_RESERVED_FOR_SU_TYPES 0x08
#define AVC_SU_TYPE_EXTEND_FIRST            0x1E    // extension from 1st 5-bits
#define AVC_SU_TYPE_UNIT                    0x1F    // the "unit" subunit
#define AVC_SU_ANY_AVAILABLE                0xFF    // any available subunit

/* subinit id */
#define AVC_SU_ID_MAX_INSTANCE_FIRST3       0x04    // first 3-bit id instances are 0-4
#define AVC_SU_ID_EXTEND_FIRST3             0x05    // extension from 1st 3-bits
#define AVC_SU_ID_reserved                  0x06
#define AVC_SU_ID_IGNORE                    0x07

#define AVC_SU_ID_EXTENDED_reserved         0x00
#define AVC_SU_ID_MIN_INSTANCE_EXTENDED     0x01    // minimum instance in extended subunit id
#define AVC_SU_ID_MAX_INSTANCE_EXTENDED     0xFE    // maximum instance in extended subunit id
#define AVC_SU_ID_EXTEND_EXTENDED           0xFF    // extension from extended byte

/* Unit commands: */
#define AVC_CMD_DIGITAL_OUTPUT              0x10
#define AVC_CMD_DIGITAL_INPUT               0x11
#define AVC_CMD_CHANNEL_USAGE               0x12
#define AVC_CMD_OUTPUT_PLUG_SIGNAL_FORMAT   0x18
#define AVC_CMD_INPUT_PLUG_SIGNAL_FORMAT    0x19
#define AVC_CMD_GENERAL_BUS_SETUP           0x1F
#define AVC_CMD_CONNECT_AV                  0x20
#define AVC_CMD_DISCONNECT_AV               0x21
#define AVC_CMD_CONNECTIONS                 0x22
#define AVC_CMD_CONNECT                     0x24
#define AVC_CMD_DISCONNECT                  0x25
#define AVC_CMD_UNIT_INFO                   0x30
#define AVC_CMD_SUBUNIT_INFO                0x31
#define AVC_CMD_STREAM_FMT_SUPPORT          0x2F    // (Stream Format Info Spec 1.1)

/* Unit and subunit commands */
#define AVC_CMD_VENDOR_DEPENDENT            0x00
#define AVC_CMD_RESERVE                     0x01
#define AVC_CMD_PLUG_INFO                   0x02
#define AVC_CMD_OPEN_INFOBLOCK              0x05
#define AVC_CMD_READ_INFOBLOCK              0x06
#define AVC_CMD_WRITE_INFOBLOCK             0x07
#define AVC_CMD_OPEN_DESCRIPTOR             0x08
#define AVC_CMD_READ_DESCRIPTOR             0x09
#define AVC_CMD_WRITE_DESCRIPTOR            0x0A
#define AVC_CMD_SEARCH_DESCRIPTOR           0x0B
#define AVC_CMD_CREATE_DESCRIPTOR           0x0C
#define AVC_CMD_OBJECT_NUMBER_SELECT        0x0D
#define AVC_CMD_SECURITY                    0x0F
#define AVC_CMD_VERSION                     0xB0
#define AVC_CMD_POWER                       0xB2
#define AVC_CMD_RATE                        0xB3
#define AVC_CMD_FUNCTION_BLOCK              0xB8
#define AVC_CMD_CHANGE_CONFIGURATION        0xC0
#define AVC_CMD_STREAM_FMT_EXT_INFO         0xBF    // (Stream Format Info Spec 1.1)

/* Unit commands (CCM) */
#define AVC_CMD_SIGNAL_SOURCE               0x1A    // (CCM 1.0 Specification)
#define AVC_CMD_INPUT_SELECT                0x1B    // ..
#define AVC_CMD_OUTPUT_PRESET               0x1C    // ..
#define AVC_CMD_CCM_PROFILE                 0x1D    // (CCM 1.1 Specification)

#endif

