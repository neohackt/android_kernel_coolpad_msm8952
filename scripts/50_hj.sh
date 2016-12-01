#!/bin/bash
export EXPERIMENTAL_USE_JAVA7=1
export PDK_FUSION_PLATFORM_ZIP=$(find -name platform.zip)
export JAVA_HOME=/usr/lib/jvm/java-1.7.0-openjdk-amd64
export CLASSPATH=.:$JAVA_HOME/lib/dt.jar:$JAVA_HOME/lib/tools.jar
export PATH=$JAVA_HOME/bin:$PATH
