/** @file shader_utils.cpp
 *
 *  @brief Utility for reading, compiling shaders and program object creation.
 *
 * @author Sylvain Beucler (OpenGL Programming wikibook), Peter Nagy (deetrone)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <GL/glew.h>

//Load file "filename" into a string in memory (this can be passed on glShaderSource)
char* file_read(const char* filename)
{
  FILE* in = fopen(filename, "rb");
  if (in == NULL) return NULL;

  int res_size = BUFSIZ;
  char* res = (char*)malloc(res_size);
  int nb_read_total = 0;

  while (!feof(in) && !ferror(in)) {
    if (nb_read_total + BUFSIZ > res_size) {
      if (res_size > 10*1024*1024) break;
      res_size = res_size * 2;
      res = (char*)realloc(res, res_size);
    }
    char* p_res = res + nb_read_total;
    nb_read_total += fread(p_res, 1, BUFSIZ, in);
  }
  
  fclose(in);
  res = (char*)realloc(res, nb_read_total + 1);
  res[nb_read_total] = '\0';
  return res;
}

void print_log(GLuint object)
{
  GLint log_length = 0;
  if (glIsShader(object)) 
  {
    glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
  }
  else if (glIsProgram(object))
  {
    glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
  }
  else
  {
    fprintf(stderr, "printlog: Not a shader or a program\n");
    return;
  }

  char* log = (char*)malloc(log_length);

  if (glIsShader(object))
  {
    glGetShaderInfoLog(object, log_length, NULL, log);
  }
  else if (glIsProgram(object))
  {
    glGetProgramInfoLog(object, log_length, NULL, log);
  }

  fprintf(stderr, "%s", log);
  free(log);
}

//Create and compile shaders from "filename"
GLuint create_shader(const char* filename, GLenum type)
{
  //read strings from "filename" and make new variable "source"
  const GLchar* source = file_read(filename);
  //error handling
  if (source == NULL) {
    fprintf(stderr, "Error opening %s: ", filename); 
    perror("");
    return 0;
  }
  GLuint res = glCreateShader(type); //create empty shader "res" of type, "type"
  const GLchar* sources[] = {
    // Define GLSL version (Open GL ES 2.0 > version 100)
    "#version 100\n"  // OpenGL ES 2.0
    ,
    // Define default float precision for fragment shaders:
    //if type=fragment ? * : "" (empty string) ... * is precision declarations.
    (type == GL_FRAGMENT_SHADER) ?
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" //if high can be used use high
    "precision highp float;           \n"
    "#else                            \n"
    "precision mediump float;         \n"
    "#endif                           \n"
     : ""
     ,
    source };

  glShaderSource(res, 3, sources, NULL); 
  free((void*)source); //free memory allocated to source void* pointer to unknown datatype
  glCompileShader(res);
  //some error handling
  GLint compile_ok = GL_FALSE;
  glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok); 
  if (compile_ok == GL_FALSE) 
  {
    fprintf(stderr, "%s:", filename);
    print_log(res);
    glDeleteShader(res);
    return 0;
  }
  return res; //GLuint "res" returned == shader object in window.cpp
}


//function that takes in "empty program" and vertex+fragment shader, and returns a program object with attached shaders 
GLuint create_program(GLuint program, GLuint vert ,GLuint frag){
    GLint link_ok = GL_FALSE;
    program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &link_ok);
    if (!link_ok) {
        fprintf(stderr, "glLinkProgram:");
        print_log(program);
        return 0;
    }
    return program;
}


