#pragma once

//==============================================================================
class Block
{
public:
  // constructor
  Block() : m_type{ 0 } {}
  Block(const signed char type) : m_type{ type } {}

  // getter
  const signed char & get() const { return m_type; }
  bool isEmpty() const { return m_type == 0; }

private:
  signed char m_type;

};