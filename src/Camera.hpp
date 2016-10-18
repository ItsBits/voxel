#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

//==================================================================================================
template<typename T>
class Camera
{
public:
    // Constructors
    Camera(const glm::tvec3<T> & position, T yaw, T pitch, T aspect_ratio);
    Camera(const glm::tvec3<T> & position);
    Camera();

    // Setter
    void update(const glm::tvec3<T> & position, const T yaw, const T pitch);
    void updateAspectRatio(const T aspect);

    // Getter
    const glm::tmat4x4<T> & getViewMatrix() const { return m_view; }
    const glm::tmat4x4<T> & getProjectionMatrix() const { return m_projection; }
    const glm::tmat4x4<T> & getViewProjectionMatrix() const { return m_view_project; }

private:
    void updateViewMatrix(const glm::tvec3<T> & position, const glm::tvec3<T> & facing);
    void updateProjectionMatrix(const T new_aspect);
    void updateVPMatrix();

    // View and projection matrix
    glm::mat4 m_projection;
    glm::mat4 m_view;
    glm::mat4 m_view_project;

    // Constants
    static const glm::vec3 WORLD_UP;
    static const float NEAR_VIEW;
    static const float FAR_VIEW;
    static const float FOV;

};

//==================================================================================================
template<typename T> const glm::vec3 Camera<T>::WORLD_UP = glm::vec3{ T(0), T(1), T(0) };
template<typename T> const float Camera<T>::NEAR_VIEW = T(0.1L);
template<typename T> const float Camera<T>::FAR_VIEW = T(500);
template<typename T> const float Camera<T>::FOV = T(1);

//==================================================================================================
template<typename T>
Camera<T>::Camera(const glm::tvec3<T> & position, T yaw, T pitch, T aspect_ratio)
{
  updateProjectionMatrix(aspect_ratio);
  update(position, yaw, pitch);
}

//==================================================================================================
template<typename T>
Camera<T>::Camera(const glm::tvec3<T> & position) : Camera{ position, T(1), T(0), T(0) } {}

//==================================================================================================
template<typename T>
Camera<T>::Camera() : Camera{ { T(0), T(0), T(0) } } {}

//==================================================================================================
template<typename T>
void Camera<T>::update(const glm::tvec3<T> & position, const T yaw, const T pitch)
{
  // update rotation
  const glm::vec3 facing = glm::normalize(
          glm::vec3{
                  std::cos(pitch) * std::cos(yaw),
                  std::sin(pitch),
                  std::cos(pitch) * std::sin(yaw)
          }
  );

  // update position
  updateViewMatrix(position, facing);
  updateVPMatrix();
}

//==================================================================================================
template<typename T>
void Camera<T>::updateAspectRatio(const T aspect)
{
  updateProjectionMatrix(aspect);
  updateVPMatrix();
}

//==================================================================================================
template<typename T>
void Camera<T>::updateViewMatrix(const glm::tvec3<T> & position, const glm::tvec3<T> & facing)
{
  m_view = glm::lookAt(position, position + facing, WORLD_UP);
}

//==================================================================================================
template<typename T>
void Camera<T>::updateProjectionMatrix(const T aspect)
{
  m_projection = glm::perspective(FOV, aspect, NEAR_VIEW, FAR_VIEW);
}

//==================================================================================================
template<typename T>
void Camera<T>::updateVPMatrix()
{
  m_view_project = m_projection * m_view;
}