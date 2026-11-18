#include "glad.h"
#include "glfw3.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <vector>

// ---------------------------------------------------------------------------
//  BALLOON POP GAME
//  - A balloon floats up from the bottom of the window.
//  - Press ANY keyboard key to POP it (ESC closes the game).
//  - Pop it before it flies off the top of the screen!
//
//  Computer Graphics topics used:
//    * Colour animation : balloon hue/brightness and background colour change over time
//    * Translation      : balloon rises and sways left-right, shards fly outward
//    * Rotation         : balloon tilts side to side, shards spin
//    * Scaling          : balloon "breathes" (pulses), shards shrink, pop flash grows
// ---------------------------------------------------------------------------

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void processInput(GLFWwindow *window);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

float aspect = (float)SCR_WIDTH / (float)SCR_HEIGHT; // width / height
bool popRequested = false;                            // set to true when a key is pressed

// The vertex shader applies: rotation -> scale -> translation (offset) -> aspect correction.
const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2  uOffset;\n"   // translation
    "uniform float uAngle;\n"    // rotation (radians)
    "uniform float uScale;\n"    // uniform scaling
    "uniform float uAspect;\n"   // window width / height so circles stay round
    "void main()\n"
    "{\n"
    "   float c = cos(uAngle);\n"
    "   float s = sin(uAngle);\n"
    "   vec2 p = vec2(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c);\n"
    "   p = p * uScale + uOffset;\n"
    "   p.x = p.x / uAspect;\n"
    "   gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\0";

const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uColor;\n"     // colour is changed every frame from the CPU
    "void main()\n"
    "{\n"
    "   FragColor = uColor;\n"
    "}\n\0";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
struct Mesh
{
    unsigned int vao = 0, vbo = 0;
    int count = 0;
};

Mesh makeMesh(const std::vector<float>& v)
{
    Mesh m;
    m.count = (int)(v.size() / 2);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    return m;
}

// ellipse as a triangle fan (centre first, then the rim)
Mesh makeEllipse(float cx, float cy, float rx, float ry, int segments)
{
    std::vector<float> v;
    v.push_back(cx);
    v.push_back(cy);
    for (int i = 0; i <= segments; i++)
    {
        float a = 2.0f * 3.14159265f * i / segments;
        v.push_back(cx + rx * std::cos(a));
        v.push_back(cy + ry * std::sin(a));
    }
    return makeMesh(v);
}

void hsv2rgb(float h, float s, float v, float &r, float &g, float &b)
{
    h = h - std::floor(h);
    float c = v * s;
    float hh = h * 6.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
    float m = v - c;
    float r1 = 0, g1 = 0, b1 = 0;
    if      (hh < 1) { r1 = c; g1 = x; }
    else if (hh < 2) { r1 = x; g1 = c; }
    else if (hh < 3) { g1 = c; b1 = x; }
    else if (hh < 4) { g1 = x; b1 = c; }
    else if (hh < 5) { r1 = x; b1 = c; }
    else             { r1 = c; b1 = x; }
    r = r1 + m; g = g1 + m; b = b1 + m;
}

float randRange(float a, float b)
{
    return a + (b - a) * (std::rand() / (float)RAND_MAX);
}

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------
const float BALLOON_START_Y = -1.5f;   // starts below the screen
const float BALLOON_END_Y   =  1.5f;   // gone above the screen
const float POP_DURATION    =  0.6f;   // seconds the pop animation lasts
const int   SHARD_COUNT     =  14;

struct Balloon
{
    float baseX, x, y;
    float speed;
    float phase;      // makes each balloon sway differently
    float hue;        // base colour
    float angle;
    bool  popped;
    float popTime;    // time since popping
    float popX, popY; // where it burst
};

void spawnBalloon(Balloon &b, int score)
{
    b.baseX   = randRange(-aspect * 0.6f, aspect * 0.6f);
    b.x       = b.baseX;
    b.y       = BALLOON_START_Y;
    b.speed   = std::fmin(0.35f + 0.04f * score, 1.1f);   // gets faster as you score
    b.phase   = randRange(0.0f, 6.28f);
    b.hue     = randRange(0.0f, 1.0f);
    b.angle   = 0.0f;
    b.popped  = false;
    b.popTime = 0.0f;
    b.popX = b.popY = 0.0f;
}

int main()
{
    std::srand((unsigned int)std::time(NULL));

    // glfw: initialize and configure
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Balloon Pop", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // build and compile our shader program
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // -----------------------------------------------------------------------
    // Geometry (all in "balloon space": balloon centre is (0,0))
    // -----------------------------------------------------------------------
    Mesh body      = makeEllipse(0.0f, 0.0f, 0.25f, 0.32f, 48);       // balloon body
    Mesh highlight = makeEllipse(-0.09f, 0.13f, 0.045f, 0.08f, 24);   // shiny spot

    // knot at the bottom of the balloon
    Mesh knot = makeMesh({ 0.0f, -0.31f,  -0.035f, -0.38f,  0.035f, -0.38f });

    // wavy string hanging below the balloon
    std::vector<float> stringV;
    for (int i = 0; i < 10; i++)
    {
        stringV.push_back(0.035f * std::sin(i * 1.3f));
        stringV.push_back(-0.38f - i * 0.06f);
    }
    Mesh stringMesh = makeMesh(stringV);

    // one shard of burst rubber (triangle)
    Mesh shard = makeMesh({ 0.0f, 0.07f,  -0.04f, -0.05f,  0.04f, -0.05f });

    int locOffset = glGetUniformLocation(shaderProgram, "uOffset");
    int locAngle  = glGetUniformLocation(shaderProgram, "uAngle");
    int locScale  = glGetUniformLocation(shaderProgram, "uScale");
    int locAspect = glGetUniformLocation(shaderProgram, "uAspect");
    int locColor  = glGetUniformLocation(shaderProgram, "uColor");

    // draw one mesh with the given transform and colour
    auto drawPart = [&](const Mesh &m, GLenum mode, float x, float y, float angle, float scale,
                        float r, float g, float b)
    {
        glUniform2f(locOffset, x, y);
        glUniform1f(locAngle, angle);
        glUniform1f(locScale, scale);
        glUniform4f(locColor, r, g, b, 1.0f);
        glBindVertexArray(m.vao);
        glDrawArrays(mode, 0, m.count);
    };

    // -----------------------------------------------------------------------
    // Game variables
    // -----------------------------------------------------------------------
    int score = 0, missed = 0;
    Balloon balloon;
    spawnBalloon(balloon, score);

    char title[128];
    std::snprintf(title, sizeof(title), "Balloon Pop | Score: %d | Missed: %d | Press ANY KEY to pop!", score, missed);
    glfwSetWindowTitle(window, title);

    std::cout << "=== BALLOON POP ===\n"
              << "Press any keyboard key to pop the balloon.\n"
              << "Pop it before it flies away! Press ESC to quit.\n";

    double lastTime = glfwGetTime();

    // render loop
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;
        float t = (float)now;

        processInput(window);

        // ------------------------- update -------------------------
        bool titleChanged = false;

        if (!balloon.popped)
        {
            balloon.y += balloon.speed * dt;                                        // translation: rise
            balloon.x  = balloon.baseX + 0.15f * std::sin(t * 1.5f + balloon.phase); // translation: sway
            balloon.angle = 0.25f * std::sin(t * 1.5f + balloon.phase + 1.2f);       // rotation: tilt

            // keyboard pressed -> pop (only if the balloon is visible on screen)
            if (popRequested && balloon.y > -0.9f)
            {
                balloon.popped  = true;
                balloon.popTime = 0.0f;
                balloon.popX = balloon.x;
                balloon.popY = balloon.y;
                score++;
                titleChanged = true;
            }
            else if (balloon.y > BALLOON_END_Y)   // escaped through the top
            {
                missed++;
                titleChanged = true;
                spawnBalloon(balloon, score);
            }
        }
        else
        {
            balloon.popTime += dt;
            if (balloon.popTime >= POP_DURATION)
                spawnBalloon(balloon, score);     // next balloon
        }
        popRequested = false;

        if (titleChanged)
        {
            std::snprintf(title, sizeof(title), "Balloon Pop | Score: %d | Missed: %d | Press ANY KEY to pop!", score, missed);
            glfwSetWindowTitle(window, title);
        }

        // ------------------------- render -------------------------
        // colour animation: slowly shifting sky colour
        float bgR, bgG, bgB;
        hsv2rgb(0.55f + 0.06f * std::sin(t * 0.3f), 0.35f, 0.85f + 0.1f * std::sin(t * 0.5f), bgR, bgG, bgB);
        glClearColor(bgR, bgG, bgB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glUniform1f(locAspect, aspect);

        if (!balloon.popped)
        {
            // colour animation: hue wobbles and brightness pulses
            float r, g, b;
            hsv2rgb(balloon.hue + 0.08f * std::sin(t * 3.0f), 0.85f, 0.85f + 0.15f * std::sin(t * 4.0f), r, g, b);

            // scaling: balloon "breathes"
            float pulse = 1.0f + 0.04f * std::sin(t * 5.0f);

            drawPart(stringMesh, GL_LINE_STRIP, balloon.x, balloon.y, balloon.angle, pulse, 0.2f, 0.2f, 0.2f);
            drawPart(knot,       GL_TRIANGLES,  balloon.x, balloon.y, balloon.angle, pulse, r * 0.7f, g * 0.7f, b * 0.7f);
            drawPart(body,       GL_TRIANGLE_FAN, balloon.x, balloon.y, balloon.angle, pulse, r, g, b);
            drawPart(highlight,  GL_TRIANGLE_FAN, balloon.x, balloon.y, balloon.angle, pulse, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            float p = balloon.popTime / POP_DURATION;   // 0 -> 1 during the pop animation

            // quick white flash that grows (scaling)
            if (p < 0.15f)
                drawPart(body, GL_TRIANGLE_FAN, balloon.popX, balloon.popY, 0.0f, 1.0f + p * 6.0f, 1.0f, 1.0f, 0.9f);

            // burst pieces fly outward (translation), spin (rotation) and shrink (scaling)
            float r, g, b;
            hsv2rgb(balloon.hue, 0.85f, 0.95f, r, g, b);
            for (int i = 0; i < SHARD_COUNT; i++)
            {
                float dirAngle = 2.0f * 3.14159265f * i / SHARD_COUNT + 0.3f * std::sin(i * 7.0f);
                float speedMul = 0.8f + 0.4f * std::fmod(i * 0.37f, 1.0f);
                float dist = (0.1f + 0.9f * p) * speedMul;
                float sx = balloon.popX + std::cos(dirAngle) * dist;
                float sy = balloon.popY + std::sin(dirAngle) * dist - 0.4f * p * p;   // slight gravity
                float spin = p * 10.0f + i;
                float sc = 1.6f * (1.0f - p);
                drawPart(shard, GL_TRIANGLES, sx, sy, spin, sc, r, g, b);
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // clean up
    Mesh meshes[] = { body, highlight, knot, stringMesh, shard };
    for (const Mesh &m : meshes)
    {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
    }
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}

// ESC closes the window
void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// any key press (except ESC) = pop request
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS && key != GLFW_KEY_ESCAPE)
        popRequested = true;
}

// keep the viewport and aspect ratio correct when the window is resized
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    if (height > 0)
        aspect = (float)width / (float)height;
}