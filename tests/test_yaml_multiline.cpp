#include <iostream>
#include <cassert>
#include "quantclaw/skill/skill_parse_file.hpp"

using namespace quantclaw;

void test_multiline_fold() {
  std::cout << "Test 1: Multi-line string with '>' (fold)..." << std::endl;
  
  std::string yaml = R"(
name: test-skill
description: >
  This is a long description
  that spans multiple lines.
  It should be folded into one line.
always: true
)";
  
  SkillFullData skill = ParseSkillFile("test.md", yaml, true);
  
  std::string expected = "This is a long description that spans multiple lines. It should be folded into one line.";
  assert(skill.mini.description == expected);
  std::cout << "  ✓ Description: '" << skill.mini.description << "'" << std::endl;
  std::cout << "  ✓ PASSED\n" << std::endl;
}

void test_multiline_preserve() {
  std::cout << "Test 2: Multi-line string with '|' (preserve)..." << std::endl;
  
  std::string yaml = R"(
name: test-skill
description: |
  Line 1
  Line 2
  Line 3
always: true
)";
  
  SkillFullData skill = ParseSkillFile("test.md", yaml, true);
  
  std::string expected = "Line 1\nLine 2\nLine 3";
  assert(skill.mini.description == expected);
  std::cout << "  ✓ Description: '" << skill.mini.description << "'" << std::endl;
  std::cout << "  ✓ PASSED\n" << std::endl;
}

void test_single_line() {
  std::cout << "Test 3: Single line string (backward compatibility)..." << std::endl;
  
  std::string yaml = R"(
name: test-skill
description: Short description
always: true
)";
  
  SkillFullData skill = ParseSkillFile("test.md", yaml, true);
  
  assert(skill.mini.description == "Short description");
  std::cout << "  ✓ Description: '" << skill.mini.description << "'" << std::endl;
  std::cout << "  ✓ PASSED\n" << std::endl;
}

void test_single_line_quoted() {
  std::cout << "Test 4: Single line quoted string..." << std::endl;
  
  std::string yaml = R"(
name: test-skill
description: "Description with 'quotes' and special chars: !"
always: true
)";
  
  SkillFullData skill = ParseSkillFile("test.md", yaml, true);
  
  assert(skill.mini.description == "Description with 'quotes' and special chars: !");
  std::cout << "  ✓ Description: '" << skill.mini.description << "'" << std::endl;
  std::cout << "  ✓ PASSED\n" << std::endl;
}

void test_weather_skill_multiline() {
  std::cout << "Test 5: Weather skill with multi-line description..." << std::endl;
  
  std::string yaml = R"(
name: weather
emoji: "\U0001F326\uFE0F"
description: >
  Check weather for any city. ALWAYS use this skill when user asks about weather,
  temperature, forecast, or climate conditions. Do NOT directly call exec tool for
  weather queries - activate this skill first with command="weather".
always: true
requires:
  bins:
    - curl
allowedTools:
  - exec
  - web_fetch
commands:
  - name: weather
    description: Check weather for a city
    toolName: exec
    argMode: freeform
)";
  
  SkillFullData skill = ParseSkillFile("test.md", yaml, true);
  
  assert(skill.mini.name == "weather");
  assert(!skill.mini.description.empty());
  assert(skill.mini.description.find("\n") == std::string::npos);  // Should be folded
  assert(skill.mini.always == true);
  assert(skill.extra.required_bins.size() == 1);
  assert(skill.extra.required_bins[0] == "curl");
  assert(skill.extra.allowed_tools.size() == 2);
  
  std::cout << "  ✓ Name: " << skill.mini.name << std::endl;
  std::cout << "  ✓ Description (folded): " << skill.mini.description << std::endl;
  std::cout << "  ✓ Required bins: curl" << std::endl;
  std::cout << "  ✓ Allowed tools: exec, web_fetch" << std::endl;
  std::cout << "  ✓ PASSED\n" << std::endl;
}

int main() {
  std::cout << "=== Testing YAML Multi-line String Support ===\n" << std::endl;
  
  try {
    test_multiline_fold();
    test_multiline_preserve();
    test_single_line();
    test_single_line_quoted();
    test_weather_skill_multiline();
    
    std::cout << "===========================================" << std::endl;
    std::cout << "✓ All tests passed!" << std::endl;
    std::cout << "===========================================" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\n✗ Test failed: " << e.what() << std::endl;
    return 1;
  }
}
