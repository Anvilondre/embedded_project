#include <PDM.h>
#include <bark_detection_inferencing.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <SoftwareSerial.h>

#define VS1053_RESET   -1
#define VS1053_CS       6     // VS1053 chip select pin (output)
#define VS1053_DCS     10     // VS1053 Data/command select pin (output)
#define CARDCS          5     // Card chip select pin
#define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 2

typedef struct {
  signed short *buffers[2];
  unsigned char buf_select;
  unsigned char buf_ready;
  unsigned int buf_count;
  unsigned int n_samples;
} inference_t;

static inference_t inference;
static bool record_ready = false;
static signed short *sampleBuffer;
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);
const int numOfTracks = 5;


void setup()
{
  Serial.begin(9600);
  run_classifier_init();
  musicPlayer.begin();
  SD.begin(CARDCS);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);
  musicPlayer.setVolume(10, 10);
  microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) 
}

void loop()
{
  bool m = microphone_inference_record();
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = {0};

  run_classifier_continuous(&signal, &result, false);

  if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
    if (result.classification[0].value >= 0.7) {
      int randomTrack = round(random(1, numOfTracks));
      musicPlayer.playFullFile(String("/track00" + String(randomTrack) + ".mp3").c_str()); 
      delay(10000);
    }
    print_results = 0;
  }
}

static void pdm_data_ready_inference_callback(void)
{
  int bytesAvailable = PDM.available();

  int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);

  if (record_ready == true) {
    for (int i = 0; i<bytesRead >> 1; i++) {
      inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];

      if (inference.buf_count >= inference.n_samples) {
        inference.buf_select ^= 1;
        inference.buf_count = 0;
        inference.buf_ready = 1;
      }
    }
  }
}

static bool microphone_inference_start(uint32_t n_samples)
{
  inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));

  if (inference.buffers[0] == NULL) {
    return false;
  }

  inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));

  if (inference.buffers[0] == NULL) {
    free(inference.buffers[0]);
    return false;
  }

  sampleBuffer = (signed short *)malloc((n_samples >> 1) * sizeof(signed short));

  if (sampleBuffer == NULL) {
    free(inference.buffers[0]);
    free(inference.buffers[1]);
    return false;
  }

  inference.buf_select = 0;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;

  PDM.onReceive(&pdm_data_ready_inference_callback);

  PDM.setBufferSize((n_samples >> 1) * sizeof(int16_t));

  PDM.setGain(127);

  record_ready = true;

  return true;
}

static bool microphone_inference_record(void)
{
  bool ret = true;

  while (inference.buf_ready == 0) {
    delay(1);
  }

  inference.buf_ready = 0;

  return ret;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
  numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);

  return 0;
}

static void microphone_inference_end(void)
{
  PDM.end();
  free(inference.buffers[0]);
  free(inference.buffers[1]);
  free(sampleBuffer);
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif