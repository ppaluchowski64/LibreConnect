package com.LibreConnect.mobile

import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureRequest
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2Interop
import androidx.camera.camera2.interop.ExperimentalCamera2Interop
import androidx.camera.core.CameraInfo
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.core.SurfaceRequest
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.FutureTask
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

object CameraFrameReceiver {
    private const val TAG = "CameraFrameReceiver"
    private const val CAMERA_PROVIDER_TIMEOUT_SECONDS = 5L
    private const val CAMERA_FRAME_RECEIVER_TIMEOUT_SECONDS = 5L
    private const val CAMERA_FALLBACK_FPS = 30

    private val cameraReceiverLock = Any()
    private var cameraProvider: ProcessCameraProvider? = null
    private var cameraPreview: Preview? = null
    private var cameraFrameExecutor: ExecutorService? = null
    private var cameraLifecycleOwner: CameraLifecycleOwner? = null
    private var videoEncoder: MediaCodec? = null
    private var videoEncoderInputSurface: Surface? = null
    private var videoEncoderDrainExecutor: ExecutorService? = null
    private val videoEncoderRunning = AtomicBoolean(false)
    private val mainThreadHandler = Handler(Looper.getMainLooper())

    private inline fun <T> runOnMainThreadBlocking(crossinline block: () -> T): T {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return block()
        }

        val task = FutureTask<T> { block() }
        mainThreadHandler.post(task)
        return task.get()
    }

    @ExperimentalCamera2Interop
    fun queryAvailableCameraConfigurations(context: Context): String {
        return try {
            val provider = ProcessCameraProvider.getInstance(context.applicationContext)
                .get(CAMERA_PROVIDER_TIMEOUT_SECONDS, TimeUnit.SECONDS)
            val cameraInfos = provider.availableCameraInfos
            val defaultCameraId = resolveDefaultCameraId(cameraInfos)
            val cameraArray = JSONArray()

            cameraInfos.forEach { cameraInfo ->
                val camera2Info = Camera2CameraInfo.from(cameraInfo)
                val cameraId = camera2Info.cameraId
                val streamConfigMap = camera2Info.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                    ?: return@forEach
                val outputSizes = streamConfigMap.getOutputSizes(ImageFormat.YUV_420_888).orEmpty()
                if (outputSizes.isEmpty()) {
                    return@forEach
                }

                val fpsRanges = camera2Info.getCameraCharacteristic(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                val fallbackFps = resolveFallbackFps(fpsRanges)
                val uniqueFormats = HashSet<String>()
                val formats = JSONArray()

                outputSizes.forEach { size ->
                    if (size.width <= 0 || size.height <= 0) {
                        return@forEach
                    }

                    val minFrameDurationNs = streamConfigMap.getOutputMinFrameDuration(ImageFormat.YUV_420_888, size)
                    val calculatedFps = if (minFrameDurationNs > 0L) {
                        (1_000_000_000L / minFrameDurationNs).toInt().coerceAtLeast(1)
                    } else {
                        fallbackFps
                    }

                    val dedupeKey = "${size.width}x${size.height}@$calculatedFps"
                    if (!uniqueFormats.add(dedupeKey)) {
                        return@forEach
                    }

                    val formatObject = JSONObject()
                    formatObject.put("width", size.width)
                    formatObject.put("height", size.height)
                    formatObject.put("framerate", calculatedFps)
                    formats.put(formatObject)
                }

                if (formats.length() == 0) {
                    return@forEach
                }

                val cameraObject = JSONObject()
                cameraObject.put("id", cameraId)
                cameraObject.put("description", resolveCameraDescription(cameraInfo, cameraId))
                cameraObject.put("isDefault", cameraId == defaultCameraId)
                cameraObject.put("formats", formats)
                cameraArray.put(cameraObject)
            }

            cameraArray.toString()
        } catch (t: Throwable) {
            Log.e(TAG, "CameraX queryAvailableCameraConfigurations failed", t)
            "[]"
        }
    }

    @ExperimentalCamera2Interop
    fun start(
        context: Context,
        requestedCameraId: String? = null,
        requestedWidth: Int = 1280,
        requestedHeight: Int = 720,
        requestedFps: Int = 30,
        requestedBitrate: Int = 2_000_000,
        onEncodedSample: (ByteBuffer, Int, Int, Long) -> Unit
    ): Boolean {
        val provider = try {
            ProcessCameraProvider.getInstance(context.applicationContext)
                .get(CAMERA_FRAME_RECEIVER_TIMEOUT_SECONDS, TimeUnit.SECONDS)
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to acquire CameraX provider", t)
            return false
        }

        return runOnMainThreadBlocking {
            synchronized(cameraReceiverLock) {
                stopLocked()

                try {
                    val targetCameraInfo = resolveTargetCameraInfo(provider.availableCameraInfos, requestedCameraId)
                    if (targetCameraInfo == null) {
                        Log.e(TAG, "No matching CameraX camera found")
                        false
                    } else {
                        val width = requestedWidth.coerceAtLeast(320)
                        val height = requestedHeight.coerceAtLeast(240)
                        val fps = requestedFps.coerceIn(1, 60)
                        val bitrate = requestedBitrate.coerceAtLeast(500_000)

                        val encoder = createHardwareEncoder(width, height, fps, bitrate)
                        if (encoder == null || videoEncoderInputSurface == null) {
                            Log.e(TAG, "Failed to create hardware H264 encoder")
                            stopLocked()
                            return@runOnMainThreadBlocking false
                        }

                        val selector = CameraSelector.Builder()
                            .addCameraFilter { infos ->
                                infos.filter { info ->
                                    Camera2CameraInfo.from(info).cameraId == Camera2CameraInfo.from(targetCameraInfo).cameraId
                                }
                            }
                            .build()

                        val previewBuilder = Preview.Builder()
                            .setTargetResolution(Size(width, height))

                        val interop = Camera2Interop.Extender(previewBuilder)
                        interop.setCaptureRequestOption(
                            CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE,
                            Range(fps, fps)
                        )
                        interop.setCaptureRequestOption(
                            CaptureRequest.CONTROL_CAPTURE_INTENT,
                            CaptureRequest.CONTROL_CAPTURE_INTENT_VIDEO_RECORD
                        )
                        interop.setCaptureRequestOption(
                            CaptureRequest.NOISE_REDUCTION_MODE,
                            CaptureRequest.NOISE_REDUCTION_MODE_FAST
                        )
                        interop.setCaptureRequestOption(
                            CaptureRequest.EDGE_MODE,
                            CaptureRequest.EDGE_MODE_FAST
                        )

                        val preview = previewBuilder.build()
                        val lifecycleOwner = CameraLifecycleOwner().also { it.start() }
                        val frameExecutor = Executors.newSingleThreadExecutor()
                        val inputSurface = videoEncoderInputSurface
                        if (inputSurface == null) {
                            Log.e(TAG, "Encoder input surface is null")
                            stopLocked()
                            return@runOnMainThreadBlocking false
                        }

                        preview.setSurfaceProvider { request ->
                            request.provideSurface(inputSurface, frameExecutor) {
                                if (it.resultCode != SurfaceRequest.Result.RESULT_SURFACE_USED_SUCCESSFULLY) {
                                    Log.w(TAG, "Camera surface request completed with code=${it.resultCode}")
                                }
                            }
                        }

                        provider.unbindAll()
                        provider.bindToLifecycle(lifecycleOwner, selector, preview)

                        startEncoderDrainLoop(encoder, onEncodedSample)

                        cameraProvider = provider
                        cameraPreview = preview
                        cameraFrameExecutor = frameExecutor
                        cameraLifecycleOwner = lifecycleOwner
                        true
                    }
                } catch (t: Throwable) {
                    Log.e(TAG, "Failed to start CameraX frame receiver", t)
                    stopLocked()
                    false
                }
            }
        }
    }

    fun stop() {
        runOnMainThreadBlocking {
            synchronized(cameraReceiverLock) {
                stopLocked()
            }
        }
    }

    private fun stopLocked() {
        videoEncoderRunning.set(false)
        videoEncoderDrainExecutor?.shutdownNow()
        videoEncoderDrainExecutor = null

        try {
            videoEncoder?.stop()
        } catch (_: Throwable) {
        }
        try {
            videoEncoder?.release()
        } catch (_: Throwable) {
        }
        videoEncoder = null

        try {
            videoEncoderInputSurface?.release()
        } catch (_: Throwable) {
        }
        videoEncoderInputSurface = null

        cameraProvider?.let { provider ->
            cameraPreview?.let { preview ->
                provider.unbind(preview)
            }
        }

        cameraLifecycleOwner?.stop()
        cameraFrameExecutor?.shutdownNow()

        cameraProvider = null
        cameraPreview = null
        cameraFrameExecutor = null
        cameraLifecycleOwner = null
    }

    private fun startEncoderDrainLoop(
        encoder: MediaCodec,
        onEncodedSample: (ByteBuffer, Int, Int, Long) -> Unit
    ) {
        videoEncoderRunning.set(true)
        val drainExecutor = Executors.newSingleThreadExecutor()
        videoEncoderDrainExecutor = drainExecutor

        drainExecutor.execute {
            val bufferInfo = MediaCodec.BufferInfo()
            try {
                while (videoEncoderRunning.get()) {
                    val outputIndex = try {
                        encoder.dequeueOutputBuffer(bufferInfo, 10_000)
                    } catch (t: Throwable) {
                        Log.e(TAG, "Encoder dequeueOutputBuffer failed", t)
                        break
                    }

                    when {
                        outputIndex >= 0 -> {
                            if (bufferInfo.size > 0) {
                                val outputBuffer = encoder.getOutputBuffer(outputIndex)
                                if (outputBuffer != null) {
                                    val duplicate = outputBuffer.duplicate()
                                    duplicate.position(bufferInfo.offset)
                                    duplicate.limit(bufferInfo.offset + bufferInfo.size)
                                    val sample = duplicate.slice()
                                    onEncodedSample(
                                        sample,
                                        bufferInfo.size,
                                        bufferInfo.flags,
                                        bufferInfo.presentationTimeUs
                                    )
                                }
                            }
                            encoder.releaseOutputBuffer(outputIndex, false)
                        }

                        outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                            Log.i(TAG, "Encoder output format changed: ${encoder.outputFormat}")
                        }

                        outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                            // No output available yet.
                        }
                    }
                }
            } catch (t: Throwable) {
                Log.e(TAG, "Encoder drain loop failed", t)
            }
        }
    }

    @ExperimentalCamera2Interop
    private fun resolveDefaultCameraId(cameraInfos: List<CameraInfo>): String? {
        val preferred = cameraInfos.firstOrNull { it.lensFacing == CameraSelector.LENS_FACING_BACK }
            ?: cameraInfos.firstOrNull()
            ?: return null
        return Camera2CameraInfo.from(preferred).cameraId
    }

    @ExperimentalCamera2Interop
    private fun resolveTargetCameraInfo(
        cameraInfos: List<CameraInfo>,
        requestedCameraId: String?
    ): CameraInfo? {
        if (!requestedCameraId.isNullOrBlank()) {
            cameraInfos.firstOrNull {
                Camera2CameraInfo.from(it).cameraId == requestedCameraId
            }?.let { return it }
        }

        return cameraInfos.firstOrNull { it.lensFacing == CameraSelector.LENS_FACING_BACK }
            ?: cameraInfos.firstOrNull()
    }

    private fun resolveFallbackFps(ranges: Array<Range<Int>>?): Int {
        val maxFps = ranges?.maxOfOrNull { range -> maxOf(range.lower, range.upper) } ?: CAMERA_FALLBACK_FPS
        return maxFps.coerceAtLeast(1)
    }

    private fun resolveCameraDescription(cameraInfo: CameraInfo, cameraId: String): String {
        val facingLabel = when (cameraInfo.lensFacing) {
            CameraSelector.LENS_FACING_FRONT -> "Front camera"
            CameraSelector.LENS_FACING_BACK -> "Back camera"
            else -> "External camera"
        }

        return "$facingLabel ($cameraId)"
    }

    private fun createHardwareEncoder(
        width: Int,
        height: Int,
        fps: Int,
        bitrate: Int
    ): MediaCodec? {
        return try {
            val codec = MediaCodec.createEncoderByType("video/avc")
            val format = MediaFormat.createVideoFormat("video/avc", width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    setInteger(MediaFormat.KEY_PREPEND_HEADER_TO_SYNC_FRAMES, 1)
                }
            }

            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            videoEncoderInputSurface = codec.createInputSurface()
            codec.start()
            videoEncoder = codec
            Log.i(TAG, "Hardware encoder configured: codec=${codec.name}, ${width}x${height}@${fps}, bitrate=$bitrate")
            codec
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to configure hardware encoder", t)
            try {
                videoEncoder?.release()
            } catch (_: Throwable) {
            }
            videoEncoder = null
            try {
                videoEncoderInputSurface?.release()
            } catch (_: Throwable) {
            }
            videoEncoderInputSurface = null
            null
        }
    }

    private class CameraLifecycleOwner : LifecycleOwner {
        private val lifecycleRegistry = LifecycleRegistry(this)

        init {
            lifecycleRegistry.currentState = Lifecycle.State.CREATED
        }

        override val lifecycle: Lifecycle
            get() = lifecycleRegistry

        fun start() {
            lifecycleRegistry.currentState = Lifecycle.State.STARTED
            lifecycleRegistry.currentState = Lifecycle.State.RESUMED
        }

        fun stop() {
            lifecycleRegistry.currentState = Lifecycle.State.DESTROYED
        }
    }
}
