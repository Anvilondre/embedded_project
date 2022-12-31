// #include <PDM.h>
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

// Struct to hold inferencing data
typedef struct {
  signed short *buffers[2];  // Buffers to hold audio data
  unsigned char buf_select;  // Index for the current buffer
  unsigned char buf_ready;   // Flag to indicate when a buffer is ready for processing
  unsigned int buf_count;    // Count of samples in the current buffer
  unsigned int n_samples;    // Total number of samples in each buffer
} inference_t;

// Declare variables
static inference_t inference;
static bool record_ready = false;
static signed short *sampleBuffer;
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);
const int numOfTracks = 5;


void setup()
{
  // Initialize serial communication and inferencing
  Serial.begin(9600);
  run_classifier_init();

  // Initialize music player and SD card
  musicPlayer.begin();
  SD.begin(CARDCS);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);
  musicPlayer.setVolume(10, 10);

  // Start recording audio for inferencing
  microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE);
}

void loop()
{
  // Record audio for inferencing
  bool m = microphone_inference_record();

  // Set up signal data for inferencing
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = {0};

  // Run inferencing on the recorded audio
  run_classifier_continuous(&signal, &result, false);

  // If enough slices have been processed, check the results and play a track if necessary
  if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
    if (result.classification[0].value >= 0.7) {
      int randomTrack = round(random(1, numOfTracks));
      musicPlayer.playFullFile(String("/" + String(randomTrack) + ".mp3").c_str()); 
      delay(10000);
    }
    print_results = 0;
  }
}

// Callback function to handle new PDM data
static void pdm_data_ready_inference_callback(void)
{
  // Get the number of bytes available in the PDM buffer
  int bytesAvailable = PDM.available();

  // Read the data from the PDM buffer into the sample buffer
  int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);

  // If recording is ready, process the data
  if (record_ready == true) {
    // Iterate through the samples in the buffer
    for (int i = 0; i<bytesRead >> 1; i++) {
      // Add the sample to the current inference buffer
      inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];

      // If the buffer is full, switch to the other buffer and reset the count
      if (inference.buf_count >= inference.n_samples) {
        inference.buf_select ^= 1;  // Toggle the buffer index (0 or 1)
        inference.buf_count = 0;  // Reset the sample count
        inference.buf_ready = 1;  // Set the buffer ready flag
      }
    }
  }
}

// Function to start recording audio for inferencing
static bool microphone_inference_start(uint32_t n_samples)
{
  // Allocate memory for the first inference buffer
  inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));

  // If the allocation fails, return false
  if (inference.buffers[0] == NULL) {
    return false;
  }

  // Allocate memory for the second inference buffer
  inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));

  // If the allocation fails, free the first buffer and return false
  if (inference.buffers[0] == NULL) {
    free(inference.buffers[0]);
    return false;
  }

  // Allocate memory for the sample buffer
  sampleBuffer = (signed short *)malloc(PDM_SAMPLE_BUFFER_SIZE);

  // If the allocation fails, free the inference buffers and return false
  if (sampleBuffer == NULL) {
    free(inference.buffers[0]);
    free(inference.buffers[1]);
    return false;
  }

  // Set the number of samples in each inference buffer
  inference.n_samples = n_samples;

  // Reset the buffer index, sample count, and ready flag
  inference.buf_select = 0;
  inference.buf_count = 0;
  inference.buf_ready = 0;

  // Set the record ready flag to true
  record_ready = true;

  // Set the callback function to handle new PDM data
  PDM.onDataReady(pdm_data_ready_inference_callback);

  // Start the PDM module
  PDM.begin();

  // Return true to indicate success
  return true;
}

// Function to record audio for inferencing
static bool microphone_inference_record(void)
{
  // If the current inference buffer is ready, reset the flag and return true
  if (inference.buf_ready == 1) {
    inference.buf_ready = 0;
    return true;
  }

  // If the current inference buffer is not ready, return false
  return false;
}

// Function to get data from the recorded audio for inferencing
static int microphone_audio_signal_get_data(void *buffer, int len, bool *is_last_chunk)
{
  // If the requested length is greater than the number of samples in the current buffer, 
  // set the length to the number of samples in the buffer
  if (len > inference.buf_count) {
    len = inference.buf_count;
  }

  // Copy the samples from the current buffer to the provided buffer
  memcpy(buffer, inference.buffers[inference.buf_select], len * sizeof(signed short));

  // Set the number of samples in the current buffer to zero
  inference.buf_count = 0;

  // Toggle the current buffer index
  inference.buf_select ^= 1;

  // Set the is_last_chunk flag to false
  *is_last_chunk = false;

  // Return the length of the data copied to the provided buffer
  return len;
}