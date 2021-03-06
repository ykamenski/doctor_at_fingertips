////////////////////////////////////////////////////////////////////////////////
// Project 5, CPSC 5530 Embedded Systems, Seattle University
// Team "ARM Brewery" 
// Edward Guevara, David Pierce, and Yevgeni Kamenski
// 
// tasks.c - 10/21/2017 4:06 PM
// Responsible for setting up the global variable sharing between tasks.
// Declares initialization functions for Task Control Blocks used by the scheduler.
// 
// This is free and unencumbered software released into the public domain.
////////////////////////////////////////////////////////////////////////////////
// @formatter:off

#include <utils/hardware_timer.h>
#include "tasks/system.h"
#include "utils/led_control.h"
#include "utils/speaker_control.h"
#include "drivers/pulse_transducer.h"
#include "tasks/commands.h"

/*****************************************************************************
 * Global variable declaration section. Declared static on purpose to allow
 * variable access only through getters.
 *****************************************************************************/

/*
 * Raw temperature readings from the sensors, initialized in the startup task
 */
static RawBuffers rawBuffers;           ///< Raw sensor buffers and indices
static ushort rawBatteryState = 200;    ///< Raw battery state reading

/*
 * Flags indicating the state of the blood pressure reading
 */
QueueHandle_t measurementCommands = NULL; ///< Queue of the measurement reqs

/*
 * Corrected sensor readings
 */
static CorrectedBuffers correctedBuffers;  ///< Corrected sensor buffers
static float batteryPercentage       = 0;  ///< Current battery in %

/**
 * Warning and alarm states of the system.
 */
static WarningAlarmStates warningAlarmStates = { 0 };

/*
 * User interface controller state
 */
static QueueHandle_t keyPressQueue = NULL;           ///< Queue of the pressed keys
static bool auralAlarmSilenced = false;              ///< when true, alarm silenced
static bool cuffControl = false;
static MeasureSelection measureSelection = MEASURE_NONE; ///< cur measure

/*
 * EKG measurements and flag when measurement completes
 */
static EKGBuffer ekgBuffer;               ///< buffer holding EKG measurements
static bool completedEKGMeasure = false;  ///< flag when measurement is done

/*
 * Command dispatcher state
 */
static QueueHandle_t cmdDispQueue = NULL; ///< Command dispatch queue

/*****************************************************************************
 * Initialize static queues
 *****************************************************************************/

static StaticQueue_t queueStaticBlockBuf[sysQUEUE_STATIC_COUNT];
static uint8_t queueStatBufKeys[sysQUEUE_LEN_KEYS * sizeof(KeyPress_t)];
static uint8_t queueStatBufCmds[sysQUEUE_LEN_CMD_PARSE * sizeof(SysCommand_t)];
static uint8_t queueStatBufMeasure[sysQUEUE_LEN_MEASURE * sizeof(MeasureSelection)];

void initStaticQueues() {
  // Queue for key strokes
  keyPressQueue = xQueueCreateStatic(sysQUEUE_LEN_KEYS,
                                     sizeof(KeyPress_t),
                                     queueStatBufKeys,
                                     &queueStaticBlockBuf[0]);

  // Queue for system commands from website and telnet
  cmdDispQueue = xQueueCreateStatic(sysQUEUE_LEN_CMD_PARSE,
                                     sizeof(SysCommand_t),
                                     queueStatBufCmds,
                                     &queueStaticBlockBuf[1]);

  measurementCommands = xQueueCreateStatic(sysQUEUE_LEN_MEASURE,
                                           sizeof(MeasureSelection),
                                           queueStatBufMeasure,
                                           &queueStaticBlockBuf[2]);

  configASSERT(keyPressQueue);
  configASSERT(cmdDispQueue);
  configASSERT(measurementCommands);
}


/*****************************************************************************
 * Task shared data declarations section.
 *****************************************************************************/

static MeasureData measureData        = {0};  ///< measure task data
static ComputeData computeData        = {0};  ///< compute task data
static DispViewModel_t dispviewModel  = {0};  ///< UI model state
static DisplayData displayData        = {0};  ///< display task data
static EnunciateData alarmWarnData    = {0};  ///< compute task data
static StatusData statusData          = {0};  ///< status task data
static ControllerData uiCtrlData      = {0};  ///< ui controller task data
static SerialCommData serialData      = {0};  ///< ui controller task data
static MeasureEKGData measEKGData     = {0};  ///< EKG measure task data
static ComputeEKGData compEKGData     = {0};  ///< EKG compute task data
static IPTaskData_t ipTaskData        = {0};  ///< IP Task references

/*****************************************************************************
 * Forward declarations of the external routines and initialization functions
 * referenced in the initializations. 
 *****************************************************************************/

extern void measure(void *arg);
extern void compute(void *arg);
extern void display(void *arg);
extern void alarmWarn(void *arg);
extern void key_scan(void *arg);
extern void uiControl(void *arg);
extern void status(void *arg);
extern void serial_comms(void *arg);
extern void measureEKG(void *arg);
extern void computeEKG(void *arg);
extern void cmdDispatch(void *arg);
extern void ipServerTask(void *arg);

/*****************************************************************************
 * Static task allocation data and routines
 *****************************************************************************/

TaskHandle_t taskHandles[sysTASK_COUNT];

static StaticTask_t taskBlockBuffers[sysTASK_COUNT];
static StackType_t taskStackBuffers[sysSTK_TOTAL];

typedef struct {
  TaskNameEnum_t id;
  TaskFunction_t pxTaskCode;
  const char *const pcName;
  uint16_t usStackDepth;
  void * const pvParameters;
  uint8_t pri;
} TCBStaticEntry_t;

#define TE(ENUM_ID,FN,DATA,PRI,STK) \
   {(ENUM_ID), (FN), (#FN), ((uint16_t)(STK)), (DATA), ((uint8_t)(PRI))}

////////////////////////////////////////////////////////////////////////////////
static const TCBStaticEntry_t taskEntries[] = {
// TaskNameEnum_t    function      argument        priority     stack size
TE(sysTCB_MEASURE   ,measure,      &measureData,   sysPRI_MEAS,  sysSTK_MEAS), 
TE(sysTCB_COMPUTE   ,compute,      &computeData,   sysPRI_COMP,  sysSTK_COMP),
TE(sysTCB_DISPLAY   ,display,      &displayData,   sysPRI_DISP,  sysSTK_DISP),
TE(sysTCB_ENUNCIATE ,alarmWarn,    &alarmWarnData, sysPRI_ENUN,  sysSTK_ENUN),
TE(sysTCB_STATUS    ,status,       &statusData,    sysPRI_STAT,  sysSTK_STAT),
TE(sysTCB_KEYSCAN   ,key_scan,     &keyPressQueue, sysPRI_KEYS,  sysSTK_KEYS),
TE(sysTCB_CONTROLLER,uiControl,    &uiCtrlData,    sysPRI_CONT,  sysSTK_CONT),
TE(sysTCB_SERIAL    ,serial_comms, &serialData,    sysPRI_SERL,  sysSTK_SERL),
TE(sysTCB_MEAS_EKG  ,measureEKG,   &measEKGData,   sysPRI_MEKG,  sysSTK_MEKG),
TE(sysTCB_COMP_EKG  ,computeEKG,   &compEKGData,   sysPRI_CEKG,  sysSTK_CEKG),
TE(sysTCB_CMD_PARSE ,cmdDispatch,  &cmdDispQueue,  sysPRI_PARSR, sysSTK_PARSR),
TE(sysTCB_SERVER    ,ipServerTask, &ipTaskData,    sysPRI_SERVR, sysSTK_SERVR),
};

#undef TE

#define initNUM_STATIC_TASKS (sizeof(taskEntries) / sizeof(TCBStaticEntry_t))

void initStaticTCBs() {
  StackType_t *curTaskStackStart = taskStackBuffers;

  for (int i = 0; i < initNUM_STATIC_TASKS; ++i) {
    // Create the task without using any dynamic memory allocation.
    taskHandles[i] = xTaskCreateStatic(
      taskEntries[i].pxTaskCode,   // Function that implements the task.
      taskEntries[i].pcName,       // Text name for the task.
      taskEntries[i].usStackDepth, // Number of indexes in the xStack array.
      taskEntries[i].pvParameters, // Parameter passed into the task.
      taskEntries[i].pri,          // Priority at which the task is created.
      curTaskStackStart,           // Array to use as the task's stack.
      &taskBlockBuffers[i]         // Var to hold the task's data structure.
    );    
    curTaskStackStart += taskEntries[i].usStackDepth;
  }
}

/*
 * Static memory for the system tasks
 */
StaticTask_t sysTaskBufs[2];
StackType_t sysStackBufs[2][configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
  StackType_t **ppxIdleTaskStackBuffer,
  uint32_t *pulIdleTaskStackSize) {
  *ppxIdleTaskTCBBuffer = sysTaskBufs;
  *ppxIdleTaskStackBuffer = sysStackBufs[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
  StackType_t **ppxTimerTaskStackBuffer,
  uint32_t *pulTimerTaskStackSize) {
  *ppxTimerTaskTCBBuffer = sysTaskBufs + 1;
  *ppxTimerTaskStackBuffer = sysStackBufs[1];
  *pulTimerTaskStackSize = configMINIMAL_STACK_SIZE;
}

// @formatter:on


/*****************************************************************************
 * Global variable initialization
 * Initialization functions to ensure all of the data structures above are
 * in the proper state upon system start-up
 *****************************************************************************/

/**
 * \brief Initializes buffers
 */
static void buffersInit() {
  // Set the raw buffer indices to first element
  rawBuffers = (RawBuffers) {
    .pressureIndex    = 0,
    .pulseRateIndex   = 0,
    .temperatureIndex = 0,
  };

  // raw buffers are initialized to the predetermined values
  for (int i = 0; i < BUF_SIZE; ++i) {
    rawBuffers.temperatures[i] = BUF_INIT_TEMP;
    rawBuffers.pressures[i] = BUF_INIT_PRESS_SYST;
    rawBuffers.pressures[i + BUF_SIZE] = BUF_INIT_PRESS_DIAS;
    rawBuffers.pulseRates[i] = BUF_INIT_PULSE;
  }

  // Set the corrected buffer indices to first element
  correctedBuffers = (CorrectedBuffers) {
    .pressureIndex    = 0,
    .pulseRateIndex   = 0,
    .temperatureIndex = 0,
    .ekgIndex = 0
  };

  // Note: corrected buffers are uninitialized
}

/**
 * \brief Initializes and returns a pointer to an initialized MEASURE TCB
 * \return pointer to an initialized TCB
 */
void globalVarsAndBuffersInit() {

  // Clear buffers
  buffersInit();

  // Initialize the Measure data pointers
  measureData = (MeasureData) {
    .rawBuffers = &rawBuffers,
    .measurementCommands = &measurementCommands
  };

  // Initialize the Compute data pointers
  computeData = (ComputeData) {
    .rawBuffers = &rawBuffers,
    .batteryState = &rawBatteryState,
    .correctedBuffers = &correctedBuffers,
    .batteryPercentage = &batteryPercentage,
  };

  // Display View model initialization
  dispviewModel = (DispViewModel_t) {
    .mode = MENU_DISP_MODE,
    .scrollPosn = 0,
    .cuffControl = &cuffControl
  };

  // Initialize the display data pointers
  displayData = (DisplayData) {
    .correctedBuffers = &correctedBuffers,
    .batteryPercentage =&batteryPercentage,
    .viewModel = &dispviewModel,
  };

  // Initialize the warning data pointers
  alarmWarnData = (EnunciateData) {
    .correctedBuffers = &correctedBuffers,
    .batteryPercentage = &batteryPercentage,
    .warningAlarmStates = &warningAlarmStates,
    .soundAlarmSilenced = &auralAlarmSilenced,
    .measurementSelection = &measureSelection
  };

  // Initialize the status data pointers
  statusData.batteryState = &rawBatteryState;

  // Initialize the ui controller data pointers
  uiCtrlData = (ControllerData) {
    .keyPressQueueHandle = keyPressQueue,
    .measurementQueue = measurementCommands,
    .viewModel = &dispviewModel,
    .auralAlarmSilenced = &auralAlarmSilenced,
    .cuffControl = &cuffControl
  };

  // Initialize the warning data pointers
  serialData = (SerialCommData) {
    .correctedBuffers = &correctedBuffers,
    .batteryPercentage = &batteryPercentage,
    .warningAlarmStates = &warningAlarmStates
  };

  // Initialize the EKG measurements pointers
  measEKGData = (MeasureEKGData) {
    .ekgBuffer = &ekgBuffer,
  };

  // Initialize the EKG measurements pointers
  compEKGData = (ComputeEKGData) {
    .ekgBuffer = &ekgBuffer,
    .correctedBuffers = &correctedBuffers,
  };

  // Initialize references for the ip task
  ipTaskData = (IPTaskData_t) {
    .correctedBuffers = &correctedBuffers,
    .warnAlarms = &warningAlarmStates
  };
}

/**
 * \brief Suspends the tasks by removing it from the execution queue. Takes no
 * action if the task is already in the suspended state
 * \param id Task to suspend
 */
void taskSuspend(const TaskNameEnum_t id) {
  for (int idx = 0; idx < initNUM_STATIC_TASKS; ++idx) {
    if (id == taskEntries[idx].id) {
      vTaskSuspend(taskHandles[idx]);
      break;
    }
  }
}

/**
 * \brief Unsuspends the tasks by adding it to the execution queue. Takes no
 * action if the task is already in the in the execution queue.
 * \param id Task to unsuspend
 */
void taskScheduleForExec(TaskNameEnum_t id){
  for (int idx = 0; idx < initNUM_STATIC_TASKS; ++idx) {
    if (id == taskEntries[idx].id) {
      vTaskResume(taskHandles[idx]);
      break;
    }
  }
}

/**
 * \brief Initializes the task control blocks and global variables
 */
void initVarsTasksQueues() {
  static bool runOnce = true; ///< ensures that this method runs only once
  if (runOnce) {
    runOnce = false;
    
    // Initialize statically-allocated Queues
    initStaticQueues();

    // Initialize the global variable links among tasks
    globalVarsAndBuffersInit();

    // Initialize statically-allocated TCBs
    initStaticTCBs();
  }
}

