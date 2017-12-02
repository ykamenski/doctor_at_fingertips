/*****************************************************************************
 * Project 3 CPSC 5530 Embedded Systems, Seattle University, 2017            *
 * Team "ARM Brewery": Edward Guevara, David Pierce, and Yevgeni Kamenski    *
 *                                                                           *
 * controller.c - 10/18/2017                                                 *
 * Responsible for the state of the user interface, dispatching tasks as
 * needed, and controlling the display characteristics
 *                                                                           *
 * "THE BEER-WARE LICENSE" (Revision 42):                                    *
 * As long as you retain this notice, you can do whatever you want with this *
 * software. If we meet some day, and you think this software is worth it,   *
 * you can buy us a beer in return. - Team "ARM Brewery"                     *
 *****************************************************************************/

#include <utils/lcd_print.h>
#include "tasks/system.h"

#define NSCROLL 4   /* Number of items available in the menu */

/**
 * \brief Processes key presses or other user input and takes appropriate
 * actions according to the current system state.
 * \param rawData raw data passed by the task scheduler
 */
void ui_controller(void *rawData) {
  ControllerData *data = (ControllerData *) rawData;
  KeyScanData *keys = data->keyScanData;

  for(;;) {
    // Only take an action if a key is pressed
    if (keys->keyAvailable) {
      // Silence the alarm when the user presses the silence key
      if (keys->keyPressedLeft) {
        *data->auralAlarmSilenced = true;
        taskScheduleForExec(sysTCB_ENUNCIATE);
      }

      // Mode selection is toggled by the right key
      if (keys->keyPressedRight) {
        *data->mode =
                (MENU_DISP_MODE == *data->mode) ? ENUNCIATE_DISP_MODE : MENU_DISP_MODE;
      }

      if (MENU_DISP_MODE == *data->mode) {
        // Menu mode

        // Scroll up when the up key is pressed
        if (keys->keyPressedUp && (*data->scrollPosn > 0)) {
          --(*data->scrollPosn);
        }

        // Scroll down when the down key is pressed
        if (keys->keyPressedDown && (*data->scrollPosn < (NSCROLL - 1))) {
          ++(*data->scrollPosn);
        }

        if (keys->keyPressedSelect) {
          switch (*data->scrollPosn) {
            case 0 :
              *data->measurementSelection = MEASURE_PRESSURE;
              *data->cuffControl = true;
              ResetPressure();
              break;
            case 1 :
              *data->measurementSelection = MEASURE_TEMPERATURE;
              *data->cuffControl = false;
              break;
            case 2 :
              *data->measurementSelection = MEASURE_PULSE;
              *data->cuffControl = false;
              break;
            case 3 :
              *data->measurementSelection = MEASURE_EKG;
              *data->cuffControl = false;
              break;
            default:
              break;
          }

          // place into enunciate mode after measurement was selected
          *data->mode = ENUNCIATE_DISP_MODE;

          //schedule measure EKG task when EKG is chosen
          if(MEASURE_EKG == *data->measurementSelection)
            taskScheduleForExec(sysTCB_MEAS_EKG);
          //schedule measure task when a measurement is chosen
          else
            taskScheduleForExec(sysTCB_MEASURE);
        }

      } else {
        // Enunciate mode
        // Increase Pressure when the up key is pressed
        if (keys->keyPressedUp && *data->cuffControl) {
          IncreasePressure();

          //schedule measure task when pressure changes
          *data->measurementSelection = MEASURE_PRESSURE;
          taskScheduleForExec(sysTCB_MEASURE);
        }

        // Decrease pressure when the down key is pressed
        if (keys->keyPressedDown && *data->cuffControl) {
          DecreasePressure();
          //schedule measure task when pressure changes
          *data->measurementSelection = MEASURE_PRESSURE;
          taskScheduleForExec(sysTCB_MEASURE);
        }
      }

      // User pressed a key, so update the display
      taskScheduleForExec(sysTCB_DISPLAY);
    }

    // Suspend controller task in schedule
    vTaskSuspend(NULL);
  }
}
