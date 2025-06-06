promise_test(async () => {
    if (window.internals)
        window.internals.setWebRTCVP9Support(false, false);

    const videoConfiguration = { contentType: 'video/vp9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    const results = await navigator.mediaCapabilities.encodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_false(results.supported, "decoder MC not supported");
    assert_not_equals(results.configuration, undefined, "decoder MC configuration");

    let codecs = RTCRtpSender.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");

    if (window.internals)
        assert_equals(vp9Codecs.length, 0, "no vp9 codec");

    if (window.internals)
        window.internals.setWebRTCVP9Support(true, false);

    codecs = RTCRtpSender.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");

    if (window.internals) {
        assert_equals(vp9Codecs.length, 1, "One vp9 codec");
        assert_equals(vp9Codecs[0].sdpFmtpLine, "profile-id=0", "profile 0");
    }

    if (window.internals)
        window.internals.setWebRTCVP9Support(true, true);

    codecs = RTCRtpSender.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");

    if (window.internals) {
        assert_equals(vp9Codecs[0].sdpFmtpLine, "profile-id=0", "first codec");
        assert_equals(vp9Codecs[1].sdpFmtpLine, "profile-id=2", "second codec");
    }
}, "VP9 in sender getCapabilities");

promise_test(async () => {
    if (window.internals)
        window.internals.setWebRTCVP9Support(false, false);

    const videoConfiguration = { contentType: 'video/vp9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    const results = await navigator.mediaCapabilities.decodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_false(results.supported, "decoder MC not supported");
    assert_not_equals(results.configuration, undefined, "decoder MC configuration");


    let codecs = RTCRtpReceiver.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");
    assert_equals(vp9Codecs.length, 0, "no vp9 codec");

    if (window.internals)
        window.internals.setWebRTCVP9Support(true, false);

    codecs = RTCRtpReceiver.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");
    assert_equals(vp9Codecs.length, 1, "One vp9 codec");
    assert_equals(vp9Codecs[0].sdpFmtpLine, "profile-id=0", "profile 0");

    if (window.internals)
        window.internals.setWebRTCVP9Support(true, true);

    codecs = RTCRtpReceiver.getCapabilities("video").codecs;
    vp9Codecs = codecs.filter(codec => codec.mimeType === "video/VP9");
    assert_equals(vp9Codecs[0].sdpFmtpLine, "profile-id=0", "first codec");
    assert_equals(vp9Codecs[1].sdpFmtpLine, "profile-id=2", "second codec");
}, "VP9 in receiver getCapabilities");

promise_test(async () => {
    let videoConfiguration = { contentType: 'video/vp9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    let results = await navigator.mediaCapabilities.decodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_true(results.supported, "decoder supported 1");
    assert_true(results.smooth, "decoder smooth 1");
    assert_not_equals(results.configuration, undefined);

    videoConfiguration = { contentType: 'video/VP9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    results = await navigator.mediaCapabilities.decodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_true(results.supported, "decoder supported 2");
    assert_not_equals(results.configuration, undefined);
    assert_true(results.smooth, "decoder smooth 2");
}, "VP9 decoding in navigator.mediaCapabilities");

promise_test(async () => {
    let videoConfiguration = { contentType: 'video/vp9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    let results = await navigator.mediaCapabilities.encodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_true(results.supported, "encoder supported 1");
    assert_equals(results.powerEfficient, results.smooth, "encoder powerEfficient 1");

    videoConfiguration = { contentType: 'video/VP9', width: 800, height: 600, bitrate: 3000, framerate: 24 };
    results = await navigator.mediaCapabilities.encodingInfo({type: 'webrtc', video: videoConfiguration });
    assert_true(results.supported, "encoder supported 2");
    assert_equals(results.powerEfficient, results.smooth, "encoder powerEfficient 2");
}, "VP9 encoding in navigator.mediaCapabilities");

promise_test(async (test) => {
    const pc = new RTCPeerConnection();
    pc.addTransceiver("video");
    const description = await pc.createOffer();
    pc.close();
    assert_true(description.sdp.indexOf("VP9") !== -1, "VP9 codec is in the SDP");
}, "Verify VP9 activation")

var track;
var remoteTrack;
var receivingConnection;
promise_test((test) => {
    return navigator.mediaDevices.getUserMedia({video: {width: 320, height: 240, facingMode: "environment"}}).then((localStream) => {
        return new Promise((resolve, reject) => {
            track = localStream.getVideoTracks()[0];

            createConnections((firstConnection) => {
                firstConnection.addTrack(track, localStream);
            }, (secondConnection) => {
                receivingConnection = secondConnection;
                secondConnection.ontrack = (trackEvent) => {
                    remoteTrack = trackEvent.track;
                    resolve(trackEvent.streams[0]);
                };
            }, { observeOffer : (offer) => {
                offer.sdp = setCodec(offer.sdp, "VP9");
                return offer;
            }
            });
            setTimeout(() => reject("Test timed out"), 5000);
        });
    }).then(async (remoteStream) => {
        video.srcObject = remoteStream;
        await video.play();

        const frame = new VideoFrame(video);
        test.add_cleanup(() => frame.close());
        assert_equals(frame.colorSpace.primaries, "bt709", "primaries");
        assert_equals(frame.colorSpace.transfer, "bt709", "transfer");
        assert_equals(frame.colorSpace.matrix, "bt709", "matrix");
    });
}, "Setting video exchange");

promise_test(() => {
    if (receivingConnection.connectionState === "connected")
        return Promise.resolve();
    return new Promise((resolve, reject) => {
        receivingConnection.onconnectionstatechange = () => {
            if (receivingConnection.connectionState === "connected")
                resolve();
        };
        setTimeout(() => reject("Test timed out"), 5000);
    });
}, "Ensuring connection state is connected");

promise_test((test) => {
    return checkVideoBlack(false, canvas1, video);
}, "Track is enabled, video should not be black");

promise_test((test) => {
    track.enabled = false;
    return checkVideoBlack(true, canvas2, video);
}, "Track is disabled, video should be black");

promise_test((test) => {
    track.enabled = true;
    return checkVideoBlack(false, canvas2, video);
}, "Track is enabled, video should not be black 2");
