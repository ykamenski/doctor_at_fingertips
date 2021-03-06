////////////////////////////////////////////////////////////////////////////////
// Project 5, CPSC 5530 Embedded Systems, Seattle University
// Team "ARM Brewery"
// Edward Guevara, David Pierce, and Yevgeni Kamenski
//
// measureEKG.c - 10/21/2017 4:00 PM
// Responsible for measuring EKG measurements using a simulation
//
// This is free and unencumbered software released into the public domain.
////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <inc/hw_ints.h>
#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <driverlib/interrupt.h>
#include <driverlib/sysctl.h>
#include <driverlib/timer.h>
#include <math.h>
#include "tasks/system.h"
#include "RTOSDebugTrace.h"

#define PI 3.14159265354f

/**
 * \brief The MeasureEKG task will measure the current EKG measurement that is
 * simulated in the Timer1IntHandler function (above)
 * \param rawData raw data passed by the task scheduler
 */

void measureEKG(void* rawData) {
  MeasureEKGData *data = (MeasureEKGData *) rawData;
  EKGBuffer *ekgBuffer = data->ekgBuffer;
  for (;;) {

    float w = 2 * PI * 1000;
    float t = 0;
    for (uint i = 0; i < BUF_SIZE_EKG; i++) {
      ekgBuffer->ekgMeasures[i] = (int)(30.0*sin(w*t));
      t += 0.000125;
    }

    FreeRTOS_debug_printf(("EKG Measured, scheduling EKG compute..\n"));
    // when done measuring, call ComputeEKG to get frequency
    taskScheduleForExec(sysTCB_COMP_EKG);
    vTaskSuspend(NULL);
  }
}