#define VERTEX_SHADER "clgl_pick_selection.vert"
#define FRAGMENT_SHADER "clgl_pick_selection.frag"
#define PROGRAM_FILE "clgl_pick_selection.cl"
#define KERNEL_FUNC "clgl_pick_selection"

// OpenCL headers
#include <CL/cl_gl.h>
#define GL_SHARING_EXTENSION "cl_khr_gl_sharing"

// OpenGL headers
#define GL3_PROTOTYPES
#include <GL3/gl3.h>
#define __gl_h_
#include <GL/freeglut.h>
#include <GL/glx.h>

// OpenGL Math Library headers
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <glm/gtc/type_ptr.hpp>

// Read from COLLADA files
#include "colladainterface.h"

#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

struct LightParameters {
  glm::vec4 diffuse_intensity;
  glm::vec4 ambient_intensity;
  glm::vec4 light_direction;
};

glm::vec3 colors[10] = {
     glm::vec3(1.0f, 1.0f, 0.0f),
     glm::vec3(0.0f, 0.0f, 1.0f),
     glm::vec3(1.0f, 0.0f, 0.0f),
     glm::vec3(0.1333f, 0.545f, 0.1333f),
     glm::vec3(0.0f, 0.0f, 0.0f),
     glm::vec3(1.0f, 0.547, 0.0f),
     glm::vec3(0.545f, 0.27f, 0.047f),
     glm::vec3(0.27f, 0.176f, 0.0f),
     glm::vec3(0.502f, 0.0f, 0.25f),
     glm::vec3(0.274f, 0.510f, 0.706f)
};
glm::vec3 white = glm::vec3(1.0f, 1.0f, 1.0f);

// OpenGL variables
glm::mat4 modelview_matrix;       // The modelview matrix
glm::mat4 mvp_matrix;             // The combined modelview-projection matrix
glm::mat4 mvp_inverse;            // Inverse of the MVP matrix
std::vector<ColGeom> geom_vec;    // Vector containing COLLADA meshes
GLuint *vaos, *vbos, *ibos;       // OpenGL buffer objects
GLuint ubo;                       // OpenGL uniform buffer object
GLint color_location;             // Index of the color uniform
GLint mvp_location;               // Index of the modelview-projection uniform
float half_height, half_width;    // Window dimensions divided in half
unsigned num_objects;             // Number of meshes in the vector
unsigned int 
   selected_object = UINT_MAX;    // Object selected by user
size_t num_triangles;             // Number of triangles in the rendering

// OpenCL variables
cl_platform_id platform;
cl_device_id device;
cl_context context;
cl_program program;
cl_command_queue queue;
cl_kernel kernel;
cl_mem vbo_memobj, ibo_memobj, t_out_buffer;
size_t max_group_size;

// Read a character buffer from a file
std::string read_file(const char* filename) {

  // Open the file
  std::ifstream ifs(filename, std::ifstream::in);
  if(!ifs.good()) {
    std::cerr << "Couldn't find the source file " << filename << std::endl;
    exit(1);
  }
  
  // Read file text into string and close stream
  std::string str((std::istreambuf_iterator<char>(ifs)), 
                   std::istreambuf_iterator<char>());
  ifs.close();
  return str;
}

// Compile the shader
void compile_shader(GLint shader) {

  GLint success;
  GLsizei log_size;
  char *log;

  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
    log = new char[log_size+1];
    log[log_size] = '\0';
    glGetShaderInfoLog(shader, log_size+1, NULL, log);
    std::cout << log;
    delete(log);
    exit(1);
  }
}

// Create, compile, and deploy shaders
GLuint init_shaders(void) {

  GLuint vs, fs, prog;
  std::string vs_source, fs_source;
  const char *vs_chars, *fs_chars;
  GLint vs_length, fs_length;

  // Create shader descriptors
  vs = glCreateShader(GL_VERTEX_SHADER);
  fs = glCreateShader(GL_FRAGMENT_SHADER);   

  // Read shader text from files
  vs_source = read_file(VERTEX_SHADER);
  fs_source = read_file(FRAGMENT_SHADER);

  // Set shader source code
  vs_chars = vs_source.c_str();
  fs_chars = fs_source.c_str();
  vs_length = (GLint)vs_source.length();
  fs_length = (GLint)fs_source.length();
  glShaderSource(vs, 1, &vs_chars, &vs_length);
  glShaderSource(fs, 1, &fs_chars, &fs_length);

  // Compile shaders and chreate program
  compile_shader(vs);
  compile_shader(fs);
  prog = glCreateProgram();

  // Bind attributes
  glBindAttribLocation(prog, 0, "in_coords");
  glBindAttribLocation(prog, 1, "in_normals");

  // Attach shaders
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);

  glLinkProgram(prog);
  glUseProgram(prog);

  return prog;
}

// Create and initialize vertex array objects (VAOs)
// and vertex buffer objects (VBOs)
void init_buffers(GLuint program) {
  
  int loc;

  // Create a VAO for each geometry
  vaos = new GLuint[num_objects];
  glGenVertexArrays(num_objects, vaos);

  // Create two VBOs for each geometry
  vbos = new GLuint[2 * num_objects];
  glGenBuffers(2 * num_objects, vbos);

  // Create an IBO for each geometry
  ibos = new GLuint[num_objects];
  glGenBuffers(num_objects, ibos);

  // Configure VBOs to hold positions and normals for each geometry
  for(unsigned int i=0; i<num_objects; i++) {

    glBindVertexArray(vaos[i]);

    // Set vertex coordinate data
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2*i]);
    glBufferData(GL_ARRAY_BUFFER, geom_vec[i].map["POSITION"].size, 
                 geom_vec[i].map["POSITION"].data, GL_STATIC_DRAW);
    loc = glGetAttribLocation(program, "in_coords");
    glVertexAttribPointer(loc, geom_vec[i].map["POSITION"].stride, 
                          geom_vec[i].map["POSITION"].type, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    // Set normal vector data
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2*i+1]);
    glBufferData(GL_ARRAY_BUFFER, geom_vec[i].map["NORMAL"].size, 
                 geom_vec[i].map["NORMAL"].data, GL_STATIC_DRAW);
    loc = glGetAttribLocation(program, "in_normals");
    glVertexAttribPointer(loc, geom_vec[i].map["NORMAL"].stride, 
                          geom_vec[i].map["NORMAL"].type, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);

    // Set index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibos[i]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 
                 geom_vec[i].index_count * sizeof(unsigned short), 
                 geom_vec[i].indices, GL_STATIC_DRAW);
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

// Initialize uniform data
void init_uniforms(GLuint program) {

  GLuint program_index, ubo_index;
  struct LightParameters params;
  glm::mat4 trans_matrix, proj_matrix;

  // Determine the locations of the color and modelview-projection matrices
  color_location = glGetUniformLocation(program, "color");
  mvp_location = glGetUniformLocation(program, "mvp");

  // Specify the modelview matrix
  trans_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.6, -5));
  modelview_matrix = glm::rotate(trans_matrix, 40.0f, glm::vec3(1, 0, 0));

  // Initialize lighting data in uniform buffer object
  params.diffuse_intensity = glm::vec4(0.4f, 0.4f, 0.4f, 1.0f);
  params.ambient_intensity = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
  params.light_direction = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

  // Set the uniform buffer object
  glUseProgram(program);
  glGenBuffers(1, &ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, ubo);
  glBufferData(GL_UNIFORM_BUFFER, 3*sizeof(glm::vec4), &params, GL_STREAM_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glUseProgram(program);

  // Match the UBO to the uniform block
  glUseProgram(program);
  ubo_index = 0;
  program_index = glGetUniformBlockIndex(program, "LightParameters");
  glUniformBlockBinding(program, program_index, ubo_index);
  glBindBufferRange(GL_UNIFORM_BUFFER, ubo_index, ubo, 0, 3*sizeof(glm::vec4));
  glUseProgram(program);
}

// Initialize OpenCL processing 
void init_cl() {

  std::string program_string;
  const char *program_chars;
  char *program_log;
  size_t program_size, log_size;
  int err;

  // Identify a platform
  err = clGetPlatformIDs(1, &platform, NULL);
  if(err < 0) {
    std::cerr << "Couldn't identify a platform" << std::endl;
    exit   (1);
  }

  // Access a device
  err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
  if(err == CL_DEVICE_NOT_FOUND) {
     err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, NULL);
  }
  if(err < 0) {
      std::cerr << "Couldn't access any devices" << std::endl;
      exit(1);   
   }

  // Create OpenCL context properties 
  cl_context_properties properties[] = {
    CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(), 
    CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(), 
    CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};

  // Create context 
  context = clCreateContext(properties, 1, &device, NULL, NULL, &err);
  if(err < 0) {
    std::cerr << "Couldn't create a context" << std::endl;
    exit(1);   
  }

  // Create program from file 
  program_string = read_file(PROGRAM_FILE);
  program_chars = program_string.c_str();
  program_size = program_string.size();
  program = clCreateProgramWithSource(context, 1, &program_chars, 
                                      &program_size, &err);
  if(err < 0) {
    std::cerr << "Couldn't create the program" << std::endl;
    exit(1);
  }

  // Build program 
  err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
  if(err < 0) {

    // Find size of log and print to std output 
    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 
                          0, NULL, &log_size);
    program_log = new char(log_size + 1);
    program_log[log_size] = '\0';
    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 
                          log_size + 1, (void*)program_log, NULL);
    std::cout << program_log << std::endl;
    delete(program_log);
    exit(1);
  }

  // Create a command queue 
  queue = clCreateCommandQueue(context, device, 0, &err);
  if(err < 0) {
    std::cerr << "Couldn't create a command queue" << std::endl;
    exit(1);   
  };

  // Create kernel 
  kernel = clCreateKernel(program, KERNEL_FUNC, &err);
  if(err < 0) {
    std::cerr << "Couldn't create a kernel: " << err << std::endl;
    exit(1);
  };

  // Determine maximum size of work groups
  clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE, 
                           sizeof(max_group_size), &max_group_size, NULL);
}

// Initialize the OpenGL Rendering
void init_gl(int argc, char* argv[]) {

  // Initialize the main window
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(300, 300);
  glutCreateWindow("Pick Sphere");
  glClearColor(0.0f, 0.25f, 0.1f, 1.0f);

  // Configure culling 
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // Enable depth testing
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LEQUAL);
  glDepthRange(0.0f, 1.0f);

  // Initialize shaders and buffers
  GLuint program = init_shaders();
  init_buffers(program);
  init_uniforms(program);
}

// Respond to paint events
void display(void) {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Draw elements of each mesh in the vector
  for(unsigned int i=0; i<num_objects; i++) {
    glBindVertexArray(vaos[i]);
    if(i != selected_object) {
       glUniform3fv(color_location, 1, &(colors[i][0])); 
    }
    else {
       glUniform3fv(color_location, 1, &(white[0])); 
    }
    glDrawElements(geom_vec[i].primitive, geom_vec[i].index_count, 
                   GL_UNSIGNED_SHORT, 0);
  }

  glBindVertexArray(0);
  glutSwapBuffers();
}

// Respond to reshape events
void reshape(int w, int h) {

  // Set window dimensions
  half_width = (float)w/2; half_height = (float)h/2;

  // Update the matrix
  mvp_matrix = glm::ortho(-2.5f, 2.5f, -2.5f, 2.5f, 3.5f, 20.0f) * modelview_matrix;
  glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(mvp_matrix[0]));

  // Compute the matrix inverse
  mvp_inverse = glm::inverse(mvp_matrix);

  // Set the viewport
  glViewport(0, 0, (GLsizei)w, (GLsizei)h);
}

// Compute selection with OpenCL
void execute_selection_kernel(glm::vec4 origin, glm::vec4 dir) {

  int err;
  float *t_out, t_test = 1000.0f;
  size_t num_groups, num_triangles, global_size;
  unsigned int i, j;

  // Create kernel arguments for the origin and direction
  err = clSetKernelArg(kernel, 0, 4*sizeof(float), glm::value_ptr(origin));
  err |= clSetKernelArg(kernel, 1, 4*sizeof(float), glm::value_ptr(dir));
  if(err < 0) {
    std::cerr << "Couldn't set a kernel argument: " << err << std::endl;
    exit(1);
  };

  // Complete OpenGL processing
  glFinish();

  for(i=0; i<num_objects; i++) {

    // Create kernel argument from VBO
    vbo_memobj = clCreateFromGLBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, vbos[2*i], &err);
    if(err < 0) {
      std::cerr << "Couldn't create a buffer object from a VBO" << std::endl;
      exit(1);
    }

    // Create kernel argument from IBO
    ibo_memobj = clCreateFromGLBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ibos[i], &err);
    if(err < 0) {
      std::cerr << "Couldn't create a buffer object from an IBO" << std::endl;
      exit(1);
    }

    // Determine global size
    num_triangles = geom_vec[i].index_count/3;
    num_groups = (size_t)(ceil((float)num_triangles/max_group_size));
    global_size = num_groups * max_group_size;

    // Allocate arrays for distance (t)
    t_out = new float[num_groups];

    // Create buffer object for distance vector
    t_out_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
                                  num_groups * sizeof(float), NULL, &err);
    if(err < 0) {
      std::cerr << "Couldn't create a buffer object: " << std::endl;
      exit(1);
    };

    // Make kernel arguments out of the VBO/IBO memory objects
    err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &vbo_memobj);
    err |= clSetKernelArg(kernel, 3, sizeof(cl_mem), &ibo_memobj);
    err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &t_out_buffer);
    err |= clSetKernelArg(kernel, 5, max_group_size*sizeof(float), NULL);
    if(err < 0) {
      std::cerr << "Couldn't set a kernel argument" << std::endl;
      exit(1);
    };

    // Acquire lock on OpenGL objects
    err = clEnqueueAcquireGLObjects(queue, 1, &vbo_memobj, 0, NULL, NULL);
    err |= clEnqueueAcquireGLObjects(queue, 1, &ibo_memobj, 0, NULL, NULL);
    if(err < 0) {
      std::cerr << "Couldn't acquire the GL objects" << std::endl;
      exit(1);   
    }

    // Execute kernel
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_size, 
                                 &max_group_size, 0, NULL, NULL);
    if(err < 0) {
      std::cerr << "Couldn't enqueue the kernel" << std::endl;
      exit(1);   
    }

    // Read t_out results
    err = clEnqueueReadBuffer(queue, t_out_buffer, CL_TRUE, 0, 
                              num_groups * sizeof(float), t_out, 0, NULL, NULL);
    if(err < 0) {
      std::cerr << "Couldn't read the buffer" << std::endl;
      exit(1);   
    }

    // Check for smallest output
    for(j=0; j<num_groups; j++) {
      if(t_out[j] < t_test) {
        t_test = t_out[j];
        selected_object = i;
      }
    }

    // Deallocate and release objects
    delete(t_out);
    clEnqueueReleaseGLObjects(queue, 1, &vbo_memobj, 0, NULL, NULL);
    clEnqueueReleaseGLObjects(queue, 1, &ibo_memobj, 0, NULL, NULL);
    clReleaseMemObject(vbo_memobj);
    clReleaseMemObject(ibo_memobj);
    clReleaseMemObject(t_out_buffer);
  }
  if(t_test == 1000) {
    selected_object = UINT_MAX;
  }

  // Release lock on OpenGL objects and redisplay window
  clFinish(queue);
  glutPostRedisplay();

}

// Respond to mouse clicks
void mouse(int button, int state, int x, int y) {

  if(state == GLUT_DOWN) {

    glm::vec3 K, L, M, E, F, G, ans;

    // Compute origin (O) and direction (D) in object coordinates
    glm::vec4 origin = mvp_inverse * glm::vec4(
             (x-half_width)/half_width, (half_height-y)/half_height, -1.0f, 1.0f);
    glm::vec4 dir = mvp_inverse * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    glm::vec4 O = glm::vec4(origin.x, origin.y, origin.z, 0.0f);
    glm::vec4 D = glm::vec4(glm::normalize(glm::vec3(dir.x, dir.y, dir.z)), 0.0f);
    execute_selection_kernel(O, D);
  }
}

// Deallocate memory
void deallocate() {

  // Deallocate mesh data
  ColladaInterface::freeGeometries(&geom_vec);

  // Deallocate OpenCL resources
  clReleaseKernel(kernel);
  clReleaseCommandQueue(queue);
  clReleaseProgram(program);
  clReleaseContext(context);

  // Deallocate OpenGL objects
  glDeleteBuffers(num_objects, ibos);
  glDeleteBuffers(2 * num_objects, vbos);
  glDeleteBuffers(num_objects, vaos);
  glDeleteBuffers(1, &ubo);
  delete(ibos);
  delete(vbos);
  delete(vaos);
}

int main(int argc, char* argv[]) {

  // Initialize COLLADA geometries
  ColladaInterface::readGeometries(&geom_vec, "spheres.dae");
  num_objects = geom_vec.size();

  // Start OpenGL processing
  init_gl(argc, argv);

  // Start OpenCL processing
  init_cl();

  // Set callback functions
  glutDisplayFunc(display);
  glutReshapeFunc(reshape);   
  glutMouseFunc(mouse);
 
  // Configure deallocation callback
  atexit(deallocate);

  // Start processing loop
  glutMainLoop();

  return 0;
}
