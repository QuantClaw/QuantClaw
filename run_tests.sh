#!/bin/bash
cd /home/jmazz/Projects/QuantClaw/build-cmake43
ninja -j4 quantclaw_tests > /home/jmazz/Projects/QuantClaw/ninja_out.txt 2>&1
echo "ninja exit: $?" >> /home/jmazz/Projects/QuantClaw/ninja_out.txt
./quantclaw_tests > /home/jmazz/Projects/QuantClaw/test_out.txt 2>&1
echo "test exit: $?" >> /home/jmazz/Projects/QuantClaw/test_out.txt
