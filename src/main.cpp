/** @file main.cpp
 *
 *  @brief implementation file of BZZZBZ main
 *
 * @author Marcell Illyes (marcellillyes), Peter Nagy (deetrone), Davide Rovelli (daviderovell0)
 *
 */

// Standard libraries
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
// Use glew.h or gl.h but first declares all prototypes
#include <GL/glew.h>
#include <GL/freeglut.h>

// SPI-related libraries
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

// Custom classes
#include "MCP3008Comm.h"
#include "AudioProcessing.h"
#include "shader_utils.h"
#include "TestLatency.h"



/***** OpenGL globals *****/
GLuint vbo_window;
GLuint program;
GLint attribute_coord3d;
GLint uniform_width;
GLint uniform_height;
GLint uniform_pA;
GLint uniform_pB;
GLint uniform_pC;
GLint uniform_fft;

struct attributes {
  GLfloat coord3d[3];
};

/***** Control interface *****/
float pot_A=0.9;
float pot_B=0.7;
float pot_C=0.6;


/***** Audio processing globals *****/

// Initalise audio processing instance with default constructor 
AudioProcessing *ap = new AudioProcessing(); 

// length of the FFT buffer is determined by the number of frames set by JACK server.
// here it is started with default settings: sr=48KHz, frames=1024, period=2, driver=alsa
const int nfft = 1024;
// audio buffer
float *audio_buffer = (float *) malloc(nfft*sizeof(float));
// lock for buffer transfer
bool audio_locked = false;
//input fft buffer
float *fft_buffer_in = new kiss_fft_scalar[nfft];
// output values to be used in the video
float *fft_frame_out = (float *) malloc((nfft/2+1)*sizeof(float));


/**
 *  Terminate the JACK client when exiting the program, to avoid errors in the 
 * following executions.
*/
void signal_handler(int sig){
	fprintf(stderr, "signal received, closing JACK client...\n");
  ap->stop();
  free(fft_frame_out);
}

/**
 * Define the audio process routine callback.
 * Get the lock and copy the buffer over. Don't do anything with the output
 * as it is already bypassed by the chip
 */
class ReadBuffer : public AudioProcessingCallback {    
    virtual void process(float* in_buffer, float* out_buffer) {
      // don't wait for the FFT buffer copy, keep reading audio
      if(!audio_locked){
        audio_locked = true;
        memcpy(audio_buffer,in_buffer, nfft*sizeof(float));
        audio_locked = false;
      }
    }
};

/**
 * SPI sample callback class
 * Processing the output samples coming from the MCP3008 ADC 
 **/
class MCP3008pollChannelsCallback : public MCP3008callback {
	virtual void hasSample(int value, int channel) {
		switch (channel)
    {
    case 4:
      pot_A = value/1024.0;
      break;
    
    case 5:
      pot_B = value/1024.0;
      break;
    
    default:
      break;
    };
	}
};


// OPENGL FUNCTIONS
/**
* \brief Initialise all resources for OpenGL: Vertex Buffer Object, 
* create shaders and attach to program object and bind attributes and uniforms
**/
int init_resources()
{ 
  //vertices, z=0
  struct attributes window_attributes[] = {
    {{ -1.0,  1.0, 0.0}},
    {{-1.0, -1.0, 0.0}},
    {{ 1.0, -1.0, 0.0}},
    {{ 1.0, 1.0, 0.0}},
    {{ -1.0, 1.0, 0.0}}
  };
  
  glGenBuffers(1, &vbo_window);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_window);
  glBufferData(GL_ARRAY_BUFFER, sizeof(window_attributes), window_attributes, GL_STATIC_DRAW);

  GLuint vs, fs;
  if ((vs = create_shader("shaders/vertex.glsl", GL_VERTEX_SHADER))   == 0) return 0;
  if ((fs = create_shader("shaders/cells.glsl", GL_FRAGMENT_SHADER)) == 0) return 0; //must be set manually for correct shader
  
  program = create_program(program, vs, fs);

  const char* attribute_name = "coord3d";
  attribute_coord3d = glGetAttribLocation(program, attribute_name);
  if (attribute_coord3d == -1) {
    fprintf(stderr, "Could not bind attribute %s\n", attribute_name);
    return 0;
  }
  //if multiple program we can choose which uniforms are bound where
  const char* uniform_name;
  uniform_name = "H";
  uniform_height = glGetUniformLocation(program, uniform_name);
  if (uniform_height == -1) {
    fprintf(stderr, "Could not bind uniform_height %s\n", uniform_name);
    return 0;
  }
  uniform_name = "W";
  uniform_width = glGetUniformLocation(program, uniform_name);
  if (uniform_width == -1) {
    fprintf(stderr, "Could not bind uniform_width %s\n", uniform_name);
    return 0;
  }

  uniform_name = "fft";
  uniform_fft = glGetUniformLocation(program, uniform_name);
  if (uniform_fft == -1) {
    fprintf(stderr, "Could not bind uniform_width %s\n", uniform_name);
    return 0;
  }
  uniform_name = "A";
  uniform_pA = glGetUniformLocation(program, uniform_name);
  if (uniform_pA == -1) {
    fprintf(stderr, "Could not bind uniform_pA %s\n", uniform_name);
    return 0;
  }
  uniform_name = "B";
  uniform_pB = glGetUniformLocation(program, uniform_name);
  if (uniform_pB == -1) {
    fprintf(stderr, "Could not bind uniform_pB %s\n", uniform_name);
    return 0;
  }
  uniform_name = "C";
  uniform_pC = glGetUniformLocation(program, uniform_name);
  if (uniform_pC == -1) {
    fprintf(stderr, "Could not bind uniform_pC %s\n", uniform_name);
    return 0;
  }
  return 1;
}

void onIdle() {
/**
*Idling function of OpenGL renderer. Wait for audio buffer to fill then copy buffer and run fft. Update uniforms such as screen size, controls, fft. Callback to display function
**/
  
  float window_width=glutGet(GLUT_WINDOW_HEIGHT); //fix viewport for correct division and no stretching
  float window_height=glutGet(GLUT_WINDOW_HEIGHT);
  
  while(audio_locked); // wait audio processing callback to finish
  audio_locked = true;
  memcpy(fft_buffer_in,audio_buffer, nfft*sizeof(float));
  audio_locked = false;
  
  // compute fft
  ap->runFFT(fft_buffer_in,fft_frame_out,nfft);
 
  // Pass values to shader
  //when switching modes change program accordingly
  glUseProgram(program);
  glUniform1f(uniform_width, window_width);
  glUniform1f(uniform_height, window_height);
  glUniform1f(uniform_pA, pot_A);
  glUniform1f(uniform_pB, pot_B);
  glUniform1f(uniform_pC, pot_C);
  glUniform1fv(uniform_fft, nfft/2+1,fft_frame_out);
  glutPostRedisplay();
}

void onDisplay()
{
/** 
* Display function of OpenGL renderer. Select program, and draw arrays from VBO. Swap between front and back buffer.
**/

  glClearColor(1.0, 1.0, 1.0, 1.0); // empty == white
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program); //must be changed from modes
  //same for all programs, as attribute_coord3d is always bound
  glEnableVertexAttribArray(attribute_coord3d);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_window);
  glVertexAttribPointer(
    attribute_coord3d,   // attribute
    3,                   // number of elements per vertex, here (x,y,z)
    GL_FLOAT,            // the type of each element
    GL_FALSE,            // take our values as-is
    0,                  //every element appears...
    0                    // offset of first element
  );

  // Push each element in buffer_vertices to the vertex shader, create rectangle
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDrawArrays(GL_TRIANGLES, 2, 3);
  glDisableVertexAttribArray(attribute_coord3d);
  glutSwapBuffers();
 

}

void free_resources() 
{   
/**
* Delete program object(s) and VBO to free up memory.
**/
  glDeleteProgram(program);
  glDeleteBuffers(1, &vbo_window);
}

int main(int argc, char *argv[]){
/**
* Main loop. Initialises classes, register callbacks. Starts threads and video rendering. Free resources on stop.
**/
    
    // Set the exit routine: Keep running until exit signal (ctrl+C) received.
    signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

    //Instantiate SPI related classes and start readouts
    MCP3008Comm* m = new MCP3008Comm();
    MCP3008pollChannelsCallback poll_cb;
    m->setCallback(&poll_cb);
    m->start();

    ReadBuffer cb;
    ap->setCallback(&cb);

    ap->start();

    // opengl
    //init context
    glutInit(&argc, argv);
    glutInitContextVersion(2,0);
    glutInitDisplayMode(GLUT_RGBA|GLUT_ALPHA|GLUT_DOUBLE|GLUT_DEPTH);
    glutInitWindowSize(glutGet(GLUT_SCREEN_WIDTH), glutGet(GLUT_SCREEN_HEIGHT));
    glutCreateWindow("BZZZBZ");
    //error handling, important for non-Pi implementations
    GLenum glew_status = glewInit();
    if (glew_status != GLEW_OK) {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
        return 1;
    } 
    if (!GLEW_VERSION_2_0) {
        fprintf(stderr, "Error: your graphic card does not support OpenGL 2.0\n");
        return 1;
    }
    //main loop if init_resources returns 1
    if (init_resources()) {
      
        glutDisplayFunc(onDisplay);
        glutIdleFunc(onIdle);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glutMainLoop();
    }

    // Terminate threads, free resources
    m->stop();
    delete m;
    ap->stop();
    free(audio_buffer);
    free(fft_buffer_in);
    free(fft_frame_out);
    free_resources();
    return 0;
}
