import mqttd.server;
import mqttd.stream;
import std.typecons;
import core.stdc.stdlib;


struct Span {
    ubyte* ptr;
    long size;
}

extern(C++) {

    interface CppConnection {
        void newMessage(const Span bytes);
        void disconnect();
    }

    interface DlangSubscriber {
        Span getWriteableBuffer();
        void handleMessages(long numBytesRead);
    }

    void startMqttServer(bool useCache) {
        gServer = typeof(gServer)(useCache ? Yes.useCache : No.useCache);
    }

    DlangSubscriber newDlangSubscriber(CppConnection connection) @nogc {
        //for some reason new/emplace causes crashes
        void[] chunk = calloc(1, Subscriber.sizeof)[0..Subscriber.sizeof];
        auto sub = cast(Subscriber)chunk.ptr;
        sub.__ctor(connection);
        return sub;
    }
}

private inout(Span) arrayToSpan(inout(ubyte)[] bytes) {
    return inout(Span)(cast(inout(ubyte)*)bytes.ptr, bytes.length);
}

private class Subscriber: DlangSubscriber {
    this(CppConnection connection) @nogc {
        _subscriber = SubscriberImpl(connection);
    }

    static struct SubscriberImpl {

        void newMessage(in ubyte[] bytes) {
            _cppConnection.newMessage(arrayToSpan(bytes));
        }

        void disconnect() {
            _cppConnection.disconnect();
        }

        CppConnection _cppConnection;
    }

    extern(C++) {
        Span getWriteableBuffer() {
            return arrayToSpan(_stream.buffer());
        }

        void handleMessages(long numBytesRead) {
            _stream.handleMessages(gServer, _subscriber);
        }
    }

private:

    MqttStream _stream;
    SubscriberImpl _subscriber;
}


private __gshared MqttServer!(Subscriber.SubscriberImpl) gServer;