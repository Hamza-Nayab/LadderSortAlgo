#!/usr/bin/env bash
git rev-parse --short HEAD 2>/dev/null || echo "release"
