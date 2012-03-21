About KXTF9 ABS_MISC(0x28) event

The sensor send events on rotation changes, but are not used in Android.
Keep this information here, just in case...

  Orientation event values:

  Pitch(Y)
  0x01 screen down                0°
  0x02 screen up                  0°

  Roll (Z)
  0x04 portrait, normal use       0°
  0x08 portrait, reversed       180°
  0x10 landscape, usb port up   270° (-90°)
  0x20 landscape, usb port down  90°


        case ABS_MISC: //0x28
            mPendingEvent.orientation.status = SENSOR_STATUS_ACCURACY_HIGH;
            mPendingEvent.type = SENSOR_TYPE_ORIENTATION;

            state = value & KXTF9_SENSOR_ROTATION_MASK; //0x3F

            // roll is the orientation used to rotate screen
            if (state == 0x04)
                mPendingEvent.orientation.roll = 0;
            else if (state == 0x8)
                mPendingEvent.orientation.roll = 2;
            else if (state == 0x10)
                mPendingEvent.orientation.roll = 3;
            else if (state == 0x20)
                mPendingEvent.orientation.roll = 1;

            // pitch is the other horizontal rotation
            mPendingEvent.orientation.pitch = (value & 0x3);

            // azimuth (vertical rotation) is not handled by this sensor event

            LOGV("SensorKXTF9: orientation event (code=0x%x, value=0x%x) state=0x%x", code, value, state);

            break;

