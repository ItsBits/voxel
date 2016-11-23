//==============================================================================
// Player.cpp
//
// Tomaž Vöröš
//==============================================================================
//
#include "Player.hpp"

#include <glm/gtx/projection.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Keyboard.hpp"
#include "Mouse.hpp"
#include <GLFW/glfw3.h>

template<typename T>
const constexpr T PI = T(3.1415926535897932384626433832795);

//==============================================================================
const float Player::SPEED_MIN{ 0.1f };
const float Player::SPEED_MAX{ 100.0f };
const float Player::SPEED_DEAFULT{ 200.0f };
const float Player::SPEED_CHANGE_FACTOR{ 3.0f };
const float Player::TIME_TO_MAX_SPEED{ 0.15f };
const glm::vec3 Player::WORLD_UP{ 0.0f, 1.0f, 0.0f };
const float Player::MAX_PITCH{ (PI<float> / 2.0f) - 0.001f };
const float Player::INVERSE_MOUSE_SENSITIVITY{ 200.0f };

//==============================================================================
Player::Player() : Player{ { 0.0f, 15.0f, 0.0f}, 0.5f * 3.14f, 0.0f } {}

//==============================================================================
Player::Player(glm::vec3 position, float yaw, float pitch) :
  m_speed{ Player::SPEED_DEAFULT },
  m_position{ position },
  //m_last_position{ position },
  m_velocity{ glm::vec3{ 0.0f, 0.0f, 0.0f } },
  m_yaw{ yaw },
  m_pitch{ pitch }
{
}

//==============================================================================
void Player::updateCameraAndItems()
{
  updateFacingDirection();
  updateCameraRotation();

  //m_active_item = static_cast<int>(Mouse::getScrollPosition().y);
}

//==============================================================================
void Player::updateFacingDirection()
{
  const auto movement = Mouse::getPointerMovement();

  const glm::vec2 offset{
     glm::vec2{ movement(0), - movement(1) } / INVERSE_MOUSE_SENSITIVITY
  };

  m_yaw += offset.x;
  m_pitch += offset.y;

  // Limit pitch
#if 1
  if (m_pitch > MAX_PITCH) m_pitch = MAX_PITCH;
  if (m_pitch < -MAX_PITCH) m_pitch = -MAX_PITCH;
#else // C++17
  m_pitch = std::clamp(m_pitch, -MAX_PITCH, MAX_PITCH);
#endif

  // Limit yaw
  m_yaw = std::fmod(m_yaw, PI<float> * 2.0f);
}

//==============================================================================
void Player::updateCameraRotation()
{
  m_facing = {
    std::cos(m_pitch) * std::cos(m_yaw),
    std::sin(m_pitch),
    std::cos(m_pitch) * std::sin(m_yaw)
  };

  m_facing = glm::normalize(m_facing);
  m_right = glm::normalize(glm::cross(m_facing, WORLD_UP));
  m_front = glm::normalize(glm::vec3{ m_facing.x, 0.0f, m_facing.z });
}

//==============================================================================
void Player::updateSpeed(const float delta_time)
{
  if (Keyboard::getKey(GLFW_KEY_Y) != Keyboard::getKey(GLFW_KEY_I))
  {
    if (Keyboard::getKey(GLFW_KEY_Y) == Keyboard::Status::PRESSED)
    {
      m_speed += m_speed * delta_time * Player::SPEED_CHANGE_FACTOR;

      if (m_speed > Player::SPEED_MAX)
        m_speed = Player::SPEED_MAX;
    }
    else
    {
      m_speed -= m_speed * delta_time * Player::SPEED_CHANGE_FACTOR;

      if (m_speed < Player::SPEED_MIN)
        m_speed = Player::SPEED_MIN;
    }
  }
}

//==============================================================================
void Player::updateVelocity(float delta_time)
{
#if 1
  // euler intergation
  const float terminal_velocity{ m_speed };
  const float mass{ 0.03f };
  const glm::vec3 gravitation{ 0.0f, -1.281f, 0.0f };


  //velocity + acceleration * time =!= 0
  //acceleration * time = - velocity
  //acceleration = - velocity / time

  //acceleration = force / mass

  //=> force / mass = - velocity / time
  //force = - velocity / time * mass

  const float PLAYER_STRENGTH{ 5.0f };

  const glm::vec3 force_to_stop{ -(m_velocity / delta_time) * mass };
  const glm::vec3 force{ getPlayerForce(force_to_stop / PLAYER_STRENGTH) * PLAYER_STRENGTH + (m_gravitation == true ? gravitation : glm::vec3{ 0.0f }) };
  //const glm::vec3 force{ 1.0f,1.0f,1.0f };
  const glm::vec3 impulse{ getPlayerImpulse() };
  const glm::vec3 acceleration{ force / mass };
  m_velocity += acceleration * delta_time;// + impulse;

  // limit horizontal speed
  const glm::vec2 horizontal_speed{ m_velocity.x, m_velocity.z };
  if (glm::length(horizontal_speed) > terminal_velocity)
  {
    const glm::vec2 new_horizontal_velocity{ glm::normalize(horizontal_speed) * terminal_velocity };
    m_velocity.x = new_horizontal_velocity.x;
    m_velocity.z = new_horizontal_velocity.y;
  }

  // limit vertical speed
  if (!m_gravitation && std::abs(m_velocity.y) > terminal_velocity)
  {
    m_velocity.y = std::copysign(terminal_velocity, m_velocity.y);
  }

  m_velocity += impulse;
  //std::cout << glm::length(m_velocity.y) << std::endl;



  // friction
  //const glm::vec3 friction_velocity{ m_velocity - glm::normalize(m_velocity) * delta_time * 1.0f }; // not good!!! breaks if framerate low!

  //const glm::vec3 temp{ friction_velocity * m_velocity };
  //m_velocity.x = Math::signum(temp.x) == 1 ? friction_velocity.x : 0.0f;
  //m_velocity.y = Math::signum(temp.y) == 1 ? friction_velocity.y : 0.0f;
  //m_velocity.z = Math::signum(temp.z) == 1 ? friction_velocity.z : 0.0f;


  //if (std::abs(m_velocity.x) > max_speed) m_velocity.x = std::copysign(max_speed, m_velocity.x);
  //if (std::abs(m_velocity.y) > max_speed) m_velocity.y = std::copysign(max_speed, m_velocity.y);
  //if (std::abs(m_velocity.z) > max_speed) m_velocity.z = std::copysign(max_speed, m_velocity.z);

#else// TODO: neds some upgrades to make it more physics-like
  const float ACCELERATION_FACTOR = m_speed * (1.0f / TIME_TO_MAX_SPEED);
  const float GRAVITATION = 10.0f;

  glm::vec3 acceleration;
  bool accelerating = getAcceleration(acceleration);

  if (accelerating)
  {
    if (acceleration.y == 0.0f)
      m_velocity.y -= GRAVITATION * delta_time;
    m_velocity += acceleration * ACCELERATION_FACTOR * delta_time;
  }
  else
  {
    float friction = ACCELERATION_FACTOR * delta_time;

    m_velocity.y -= GRAVITATION * delta_time;

    if (friction > glm::length(m_velocity))
      m_velocity = glm::vec3{ 0.0f };
    else
      m_velocity -= glm::normalize(m_velocity) * friction;
  }

  // Limit max speed
  if (glm::length(m_velocity) > m_speed)
    m_velocity = glm::normalize(m_velocity) * m_speed;
#endif

  // moveWithRespectToCollision(delta_time);
}


//==============================================================================
void Player::applyVelocity(float delta_time)
{
  // Euler integration

  //m_last_position = m_position;
#if 1
  m_position += m_velocity * delta_time;
#else
  //m_position += m_velocity * delta_time;
  /*
  // DO NOT USE
  Ray movement{ m_position, m_velocity * delta_time };
  m_position = movement.collisionResponse(world);
  */
  //-------------------------
  float rest_time = delta_time;
NEW_ROUND:
  if (rest_time < 0.001f) return;
  const glm::vec3 player_pos = m_position + m_velocity * rest_time;


  const AABB player{
    player_pos - player_size,
    player_pos + player_size,
  };
  const AABB player_before{
    m_position - player_size,
    m_position + player_size,
  };

  // could be converted to AABB<int> <- template
  const glm::ivec3 player_min{
    Math::floor(player.getMin().x),
    Math::floor(player.getMin().y),
    Math::floor(player.getMin().z)
  };

  const glm::ivec3 player_max{
    Math::floor(player.getMax().x),
    Math::floor(player.getMax().y),
    Math::floor(player.getMax().z)
  };

  AABB::Collision first_collision{ 1.0f,{ 1.0f, 1.0f, 1.0f } };
  glm::ivec3 block_pos;
  for (block_pos.z = player_min.z; block_pos.z <= player_max.z; ++block_pos.z)
    for (block_pos.y = player_min.y; block_pos.y <= player_max.y; ++block_pos.y)
      for (block_pos.x = player_min.x; block_pos.x <= player_max.x; ++block_pos.x)
      {
        if (world.getBlock(block_pos) == false) continue;

        const AABB block{ // cast to float
          block_pos,
          glm::vec3{ block_pos } +glm::vec3{ 1.0f, 1.0f, 1.0f }
        };

        /*
        float distance = AABB::distance(block, player);

        if (distance < 0.0f)
        {
        m_position = m_last_position;
        return;
        }
        */
        AABB::Collision this_collision{ AABB::collisionTime(player_before, m_velocity, block) };

        if (first_collision.time > this_collision.time)
          first_collision = this_collision;

      }

  if (first_collision.time < 1.0f)
  {
    if (first_collision.face.y == 0.0f && m_velocity.y < 0.0f)
      m_on_ground = true;
    m_position += m_velocity * rest_time * first_collision.time;
    m_velocity *= first_collision.face;
    rest_time -= first_collision.time * rest_time;


    goto NEW_ROUND;
  }
  else
  {
    m_position += m_velocity * rest_time * first_collision.time;
  }
#endif
}

//==============================================================================
void Player::maskVelocity(glm::vec3 mask)
{
  m_velocity *= mask;
}



//==============================================================================
bool Player::getAcceleration(glm::vec3 & acceleration) const
{
  bool accelerating = false;

  if (Keyboard::getKey(GLFW_KEY_U) != Keyboard::getKey(GLFW_KEY_J))
  {
    if (Keyboard::getKey(GLFW_KEY_U) == Keyboard::Status::PRESSED)
      acceleration += m_front;
    else
      acceleration -= m_front;

    accelerating = true;
  }

  if (Keyboard::getKey(GLFW_KEY_K) != Keyboard::getKey(GLFW_KEY_H))
  {
    if (Keyboard::getKey(GLFW_KEY_K) == Keyboard::Status::PRESSED)
      acceleration += m_right;
    else
      acceleration -= m_right;

    accelerating = true;
  }

  if (Keyboard::getKey(GLFW_KEY_L) != Keyboard::getKey(GLFW_KEY_SPACE))
  {
    if (Keyboard::getKey(GLFW_KEY_L) == Keyboard::Status::PRESSED)
      acceleration += Player::WORLD_UP;
    else
      acceleration -= Player::WORLD_UP;

    accelerating = true;
  }

  //accelerating = true;
  //acceleration += m_front * 0.5f;

  // if (accelerating) acceleration = glm::normalize(acceleration);

  return accelerating;
}
//==============================================================================
glm::vec3 Player::getPlayerForce(const glm::vec3 & force_to_stop) const
{
  glm::vec3 force{ 0.0f, 0.0f, 0.0f };

  //----------------------------------
  if (Keyboard::getKey(GLFW_KEY_U) != Keyboard::getKey(GLFW_KEY_J))
  {
    if (Keyboard::getKey(GLFW_KEY_U) == Keyboard::Status::PRESSED)
      force += m_front;
    else
      force -= m_front;
  }
  else
  {
    const glm::vec3 projected_front{ glm::proj(force_to_stop, m_front) };
    const bool stop{ glm::length(projected_front) < glm::length(m_front) };

    if (glm::dot(glm::vec3{ m_velocity.x, 0.0f, m_velocity.z }, m_front) < 0.0f)
    {
      if (stop == true)
        force += projected_front;
      else
        force += m_front;
    }
    else
    {
      if (stop == true)
        force += projected_front; // because it is pointing in right tirection
      else
        force -= m_front;
    }
  }
  //----------------------------------
  if (Keyboard::getKey(GLFW_KEY_K) != Keyboard::getKey(GLFW_KEY_H))
  {
    if (Keyboard::getKey(GLFW_KEY_K) == Keyboard::Status::PRESSED)
      force += m_right;
    else
      force -= m_right;
  }
  else
  {
    const glm::vec3 projected_right{ glm::proj(force_to_stop, m_right) };
    const bool stop{ glm::length(projected_right) < glm::length(m_right) };

    if (glm::dot(glm::vec3{ m_velocity.x, 0.0f, m_velocity.z }, m_right) < 0.0f)
    {
      if (stop == true)
        force += projected_right;
      else
        force += m_right;
    }
    else
    {
      if (stop == true)
        force += projected_right; // because it is pointing in right tirection
      else
        force -= m_right;
    }
  }
  //----------------------------------
  if (m_gravitation == true)
    goto EXIT;
  if (Keyboard::getKey(GLFW_KEY_L) != Keyboard::getKey(GLFW_KEY_SPACE))
  {
    if (Keyboard::getKey(GLFW_KEY_L) == Keyboard::Status::PRESSED)
      force += Player::WORLD_UP;
    else
      force -= Player::WORLD_UP;
  }
  else
  {
    const bool stop{ std::abs(force_to_stop.y) < std::abs(Player::WORLD_UP.y) };
    if (m_velocity.y < 0.0f)
    {
      if (stop == true)
        force += glm::vec3{ 0.0f, force_to_stop.y, 0.0f };
      else
        force += Player::WORLD_UP;
    }
    else
    {
      if (stop == true)
        force += glm::vec3{ 0.0f, force_to_stop.y, 0.0f }; // because force_to_stop.y is pointing in right tirection
      else
        force -= Player::WORLD_UP;
    }
  }

EXIT:
  return force;
}

//==============================================================================
glm::vec3 Player::getPlayerImpulse()
{
  return { 0.0f, 0.0f, 0.0f };
  /*
  const glm::vec3 feet_position{ m_position - glm::vec3{ 0.0f, player_size.y + 0.01f, 0.0f } };
  const bool on_ground{ world.getBlock(World::posToBlock(feet_position)) != 0 };

  static int counter{ 0 };

  if (
    m_on_ground
    &&
    Keyboard::getKey(GLFW_KEY_SPACE) == Keyboard::Status::PRESSED
    &&
    m_gravitation == true
    )
  {
    m_on_ground = false;
    ++counter;
    //std::cout << "----- " << counter << std::endl;
    return glm::vec3{ 0.0f, 10.0f, 0.0f };
  }
  return glm::vec3{ 0.0f, 0.0f, 0.0f };
  */
}