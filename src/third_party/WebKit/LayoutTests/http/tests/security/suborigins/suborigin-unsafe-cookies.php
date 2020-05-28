<?php
setcookie("test_cookie", "a_value");
header("Suborigin: foobar 'unsafe-cookies';");
?>
<!DOCTYPE html>
<html>
<head>
<script src="/resources/testharness.js"></script>
<script src="/resources/testharnessreport.js"></script>
</head>
<body>
<script>
test(t => {
    assert_equals(document.cookie, "test_cookie=a_value");
    document.cookie = "foo=bar";
    assert_equals(document.cookie, "test_cookie=a_value; foo=bar");
    t.done();
}, "Document is not cookie-averse");

test(t => {
    Object.defineProperties(document, {
        "cookie": {
            get: function() { return this.x; },
            set: function(x) { this.x = x; }
        }
    });

    document.cookie = "foo";
    assert_equals(document.cookie, "foo");
    delete document.cookie;
    t.done();
}, "Document getters and setters still work");

async_test(t => {
    window.addEventListener('message', function(event) {
        if (event.data.test_name != "iframetest")
            return;

        var cookie_val = event.data.cookie_val;
        assert_equals(cookie_val, "test_cookie=a_value; foo=bar");
        t.done();
    });

    var iframe = document.createElement('iframe');
    iframe.src = "resources/post-document-cookie.php?testname=iframetest";
    document.body.appendChild(iframe);
}, "Cookies set in a frame with a regular, same-origin src modify the suborigin's document.cookie");

function makeIframeString(test_name) {
    var postMessageContent = "{cookie_val: document.cookie, test_name: '" + test_name + "'}";
    return "document.cookie = 'foo=bar'; window.parent.postMessage(" + postMessageContent + ", '*')";
}

async_test(t => {
    window.addEventListener('message', function(event) {
        if (event.data.test_name != "about:blanktest")
            return;

        assert_equals(event.data.cookie_val, "test_cookie=a_value; foo=bar");
        t.done();
    });

    var iframe = document.createElement('iframe');
    iframe.src = "about:blank";
    iframe.onload = function() {
        var script = iframe.contentWindow.document.createElement("script");
        script.innerHTML = makeIframeString("about:blanktest");
        iframe.contentWindow.document.body.appendChild(script);
    };
    document.body.appendChild(iframe);
}, "Cookies set in an about:blank frame modify the suborigin's document.cookie and also have the same document.cookie");

async_test(t => {
    window.addEventListener('message', function(event) {
        if (event.data.test_name != "blob:test")
            return;

        assert_equals(event.data.cookie_val, "");
        t.done();
    });

    var iframe = document.createElement('iframe');
    var script = "<" + "script>" + makeIframeString("blob:test") + "<" + "/script>";
    var blob = new Blob([script], {type: 'text/html'});
    iframe.src = URL.createObjectURL(blob);
    document.body.appendChild(iframe);
}, "Cookies set in a blob: frame do not modify the suborigin's document.cookie and also have an empty document.cookie (blobs are still different origin)");

async_test(t => {
    window.addEventListener('message', function(event) {
        if (event.data.test_name != "srcdoc:test")
            return;

        assert_equals(event.data.cookie_val, "test_cookie=a_value; foo=bar");
        t.done();
    });

    var iframe = document.createElement('iframe');
    var script = "<" + "script>" + makeIframeString("srcdoc:test") + "<" + "/script>";
    iframe.srcdoc = "srcdoc:" + script;
    document.body.appendChild(iframe);
}, "Cookies set in a srcdoc frame modify the suborigin's document.cookie and also have the same document.cookie");

</script>
</body>
</html>
