//==============================================================================
// Player.hpp
//
// Tomaž Vöröš
//==============================================================================
//
#pragma once

// TODO: refactor

#include <glm/glm.hpp>

class Player
{
public:
    Player();
    Player(glm::vec3 position, float yaw, float pitch);

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getVelocity() const { return m_velocity; }
    glm::vec3 getViewDirection() const { return m_facing; }

    void updateCameraAndItems();
    void updateVelocity(float delta_time);
    void applyVelocity(float delta_time);
    void maskVelocity(glm::vec3 mask);

    float getYaw() const { return m_yaw; }
    float getPitch() const { return m_pitch; }

    int getHeldItem() const { return m_active_item; }
    void updateSpeed(const float new_speed);

private:
    void updateFacingDirection();
    void updateCameraRotation();

    static const float MAX_PITCH;
    static const float INVERSE_MOUSE_SENSITIVITY;

    static const float SPEED_MIN;
    static const float SPEED_MAX;
    static const float SPEED_DEAFULT;
    static const float SPEED_CHANGE_FACTOR;
    static const float TIME_TO_MAX_SPEED;
    static const glm::vec3 WORLD_UP;

    bool m_gravitation{ false };

    bool m_on_ground{ false };


    bool getAcceleration(glm::vec3 & acceleration) const;
    glm::vec3 getPlayerForce(const glm::vec3 & force_to_stop) const;
    glm::vec3 getPlayerImpulse();


    // currently held item
    int m_active_item;

    // Maximum movement speed
    float m_speed;

    // Camera position
    glm::vec3 m_position;
    //glm::vec3 m_last_position;
    glm::vec3 m_velocity;

    // Euler angles
    float m_yaw;
    float m_pitch;

    // Normalized vectors
    glm::vec3 m_facing;
    glm::vec3 m_front;
    glm::vec3 m_right;


    const glm::vec3 player_size{ 0.4f, 1.4f, 0.4f };

};