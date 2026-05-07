# Keep JNI-facing NEXUS node classes so native methods remain callable after minification
-keep class com.nexus.mesh.service.NexusNode { *; }
-keep class com.nexus.mesh.service.NexusNode$Callback { *; }
