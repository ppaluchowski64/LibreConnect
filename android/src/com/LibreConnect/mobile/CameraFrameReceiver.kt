package com.LibreConnect.mobile

import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.*
import android.hardware.camera2.params.StreamConfigurationMap
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.*
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import android.view.SurfaceHolder
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

object CameraFrameReceiver {
    private const val TAG = "CameraFrameReceiver"
    private const val CAMERA_FALLBACK_FPS = 30

    private val cameraReceiverLock = Any()
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var videoEncoder: MediaCodec? = null
    private var videoEncoderInputSurface: Surface? = null
    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null
    private val isRunning = AtomicBoolean(false)

    fun queryAvailableCameraConfigurations(context: Context): String {
        return try {
            val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
            val cameraArray = JSONArray()

            cameraManager.cameraIdList.forEach { cameraId ->
                val chars = cameraManager.getCameraCharacteristics(cameraId)
                val configMap = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return@forEach
                
                // Use MediaCodec as a proxy for hardware encoding capabilities
                val outputSizes = configMap.getOutputSizes(MediaCodec::class.java).orEmpty()
                if (outputSizes.isEmpty()) return@forEach

                val fpsRanges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                val fallbackFps = resolveFallbackFps(fpsRanges)
                val uniqueFormats = HashSet<String>()
                val formats = JSONArray()

                outputSizes.forEach { size ->
                    if (size.width <= 0 || size.height <= 0) return@forEach

                    val minFrameDurationNs = configMap.getOutputMinFrameDuration(SurfaceHolder::class.java, size)
                    val calculatedFps = if (minFrameDurationNs > 0L) {
                        (1_000_000_000L / minFrameDurationNs).toInt().coerceAtLeast(1)
                    } else {
                        fallbackFps
                    }

                    val dedupeKey = "${size.width}x${size.height}@$calculatedFps"
                    if (!uniqueFormats.add(dedupeKey)) return@forEach

                    val formatObject = JSONObject()
                    formatObject.put("width", size.width)
                    formatObject.put("height", size.height)
                    formatObject.put("framerate", calculatedFps)
                    formats.put(formatObject)
                }

                if (formats.length() == 0) return@forEach

                val cameraObject = JSONObject()
                cameraObject.put("id", cameraId)
                cameraObject.put("description", resolveCameraDescription(chars, cameraId))
                cameraObject.put("isDefault", chars.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK)
                cameraObject.put("formats", formats)
                cameraArray.put(cameraObject)
            }

            cameraArray.toString()
        } catch (t: Throwable) {
            Log.e(TAG, "Camera2 queryAvailableCameraConfigurations failed", t)
            "[]"
        }
    }

    fun start(
        context: Context,
        requestedCameraId: String? = null,
        requestedWidth: Int = 1280,
        requestedHeight: Int = 720,
        requestedFps: Int = 30,
        requestedBitrate: Int = 2_000_000,
        onEncodedSample: (ByteBuffer, Int, Int, Long) -> Unit
    ): Boolean {
        synchronized(cameraReceiverLock) {
            stopLocked()

            try {
                val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
                val cameraId = requestedCameraId ?: cameraManager.cameraIdList.firstOrNull {
                    cameraManager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
                } ?: cameraManager.cameraIdList.firstOrNull()
                if (cameraId == null) {
                    Log.e(TAG, "Failed to start camera: No camera IDs found on this device")
                    return false
                }

                val width = requestedWidth.coerceAtLeast(320)
                val height = requestedHeight.coerceAtLeast(240)
                val fps = requestedFps.coerceIn(1, 60)
                val bitrate = requestedBitrate.coerceAtLeast(500_000)

                startBackgroundThread()

                val encoder = createHardwareEncoder(width, height, fps, bitrate, onEncodedSample)
                if (encoder == null || videoEncoderInputSurface == null) {
                    Log.e(TAG, "Failed to start camera: hardware encoder or input surface creation failed")
                    stopLocked()
                    return false
                }

                isRunning.set(true)
                
                cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                    override fun onOpened(camera: CameraDevice) {
                        synchronized(cameraReceiverLock) {
                            if (!isRunning.get()) {
                                camera.close()
                                return
                            }
                            cameraDevice = camera
                            createCaptureSession(camera, width, height, fps)
                        }
                    }

                    override fun onDisconnected(camera: CameraDevice) {
                        stop()
                    }

                    override fun onError(camera: CameraDevice, error: Int) {
                        Log.e(TAG, "CameraDevice error: $error")
                        stop()
                    }
                }, backgroundHandler)

                return true
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to start Camera2 frame receiver", t)
                stopLocked()
                return false
            }
        }
    }

    fun stop() {
        synchronized(cameraReceiverLock) {
            stopLocked()
        }
    }

    private fun stopLocked() {
        isRunning.set(false)
        
        try {
            captureSession?.stopRepeating()
            captureSession?.close()
        } catch (_: Throwable) {}
        captureSession = null

        cameraDevice?.close()
        cameraDevice = null

        try {
            videoEncoder?.stop()
            videoEncoder?.release()
        } catch (_: Throwable) {}
        videoEncoder = null

        videoEncoderInputSurface?.release()
        videoEncoderInputSurface = null

        stopBackgroundThread()
    }

    private fun startBackgroundThread() {
        backgroundThread = HandlerThread("CameraFrameReceiverThread").apply { start() }
        backgroundHandler = Handler(backgroundThread!!.looper)
    }

    private fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
        } catch (_: InterruptedException) {}
        backgroundThread = null
        backgroundHandler = null
    }

    private fun createCaptureSession(camera: CameraDevice, width: Int, height: Int, fps: Int) {
        val surface = videoEncoderInputSurface ?: return
        
        val captureRequestBuilder = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
            addTarget(surface)
            set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(fps, fps))
            set(CaptureRequest.CONTROL_CAPTURE_INTENT, CaptureRequest.CONTROL_CAPTURE_INTENT_VIDEO_RECORD)
            set(CaptureRequest.NOISE_REDUCTION_MODE, CaptureRequest.NOISE_REDUCTION_MODE_FAST)
            set(CaptureRequest.EDGE_MODE, CaptureRequest.EDGE_MODE_FAST)
        }

        camera.createCaptureSession(listOf(surface), object : CameraCaptureSession.StateCallback() {
            override fun onConfigured(session: CameraCaptureSession) {
                synchronized(cameraReceiverLock) {
                    if (!isRunning.get()) {
                        session.close()
                        return
                    }
                    captureSession = session
                    try {
                        session.setRepeatingRequest(captureRequestBuilder.build(), null, backgroundHandler)
                    } catch (e: CameraAccessException) {
                        Log.e(TAG, "Failed to start repeating request", e)
                        stop()
                    }
                }
            }

            override fun onConfigureFailed(session: CameraCaptureSession) {
                Log.e(TAG, "CameraCaptureSession configuration failed")
                stop()
            }
        }, backgroundHandler)
    }

    private fun createHardwareEncoder(
        width: Int,
        height: Int,
        fps: Int,
        bitrate: Int,
        onEncodedSample: (ByteBuffer, Int, Int, Long) -> Unit
    ): MediaCodec? {
        return try {
            val pixels = width * height
            val mimeType = if (pixels > 2073600) MediaFormat.MIMETYPE_VIDEO_HEVC else MediaFormat.MIMETYPE_VIDEO_AVC
            val codec = MediaCodec.createEncoderByType(mimeType)
            val format = MediaFormat.createVideoFormat(mimeType, width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    setInteger(MediaFormat.KEY_PREPEND_HEADER_TO_SYNC_FRAMES, 1)
                }
            }

            codec.setCallback(object : MediaCodec.Callback() {
                override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                    // Not used for Surface input
                }

                override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                    if (info.size > 0) {
                        codec.getOutputBuffer(index)?.let { buffer ->
                            buffer.position(info.offset)
                            buffer.limit(info.offset + info.size)
                            onEncodedSample(buffer, info.size, info.flags, info.presentationTimeUs)
                        }
                    }
                    codec.releaseOutputBuffer(index, false)
                }

                override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                    Log.e(TAG, "MediaCodec error", e)
                    stop()
                }

                override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                    Log.i(TAG, "Encoder output format changed: $format")
                }
            }, backgroundHandler)

            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            videoEncoderInputSurface = codec.createInputSurface()
            codec.start()
            videoEncoder = codec
            Log.i(TAG, "Hardware encoder configured: codec=${codec.name}, ${width}x${height}@${fps}, bitrate=$bitrate")
            codec
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to configure hardware encoder", t)
            videoEncoder?.release()
            videoEncoder = null
            videoEncoderInputSurface?.release()
            videoEncoderInputSurface = null
            null
        }
    }

    private fun resolveFallbackFps(ranges: Array<Range<Int>>?): Int {
        val maxFps = ranges?.maxOfOrNull { range -> maxOf(range.lower, range.upper) } ?: CAMERA_FALLBACK_FPS
        return maxFps.coerceAtLeast(1)
    }

    private fun resolveCameraDescription(chars: CameraCharacteristics, cameraId: String): String {
        val facing = chars.get(CameraCharacteristics.LENS_FACING)
        val facingLabel = when (facing) {
            CameraCharacteristics.LENS_FACING_FRONT -> "Front camera"
            CameraCharacteristics.LENS_FACING_BACK -> "Back camera"
            else -> "External camera"
        }
        return "$facingLabel ($cameraId)"
    }
}
