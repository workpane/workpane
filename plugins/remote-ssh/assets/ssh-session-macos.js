ObjC.import("Foundation")
ObjC.bindFunction("getsid", ["int", ["int"]])
ObjC.bindFunction("getpid", ["int", []])
ObjC.bindFunction("kill", ["int", ["int", "int"]])

function run(arguments) {
    const method = arguments[0]
    const identity = Number(arguments[1])

    if (!Number.isInteger(identity) || identity < 2 || identity > 2147483647) {
        throw new Error("The SSH session identity is invalid")
    }

    if (method === "session" && arguments.length === 2) {
        const session = $.getsid(identity)

        if (session < 2) {
            throw new Error("The SSH session could not be inspected")
        }

        return String(session)
    }

    const signal = Number(arguments[2])

    if (method !== "signal" || arguments.length !== 3 || signal !== 1 && signal !== 15 && signal !== 9) {
        throw new Error("The SSH session operation is invalid")
    }

    const input = $.NSFileHandle.fileHandleWithStandardInput
    const content = $.NSMutableData.data

    while (true) {
        const bytes = input.readDataOfLength(65536)

        if (Number(bytes.length) === 0) {
            break
        }

        content.appendData(bytes)

        if (content.length > 1048576) {
            throw new Error("The SSH process listing exceeds its bound")
        }
    }

    const source = $.NSString.alloc.initWithDataEncoding(content, $.NSUTF8StringEncoding)

    if (!source) {
        throw new Error("The SSH process listing is invalid")
    }

    const processes = ObjC.unwrap(source).trim().split(/\s+/)

    if (processes.length > 65536) {
        throw new Error("The SSH process count exceeds its bound")
    }

    let stopped = 0

    if (signal !== 9 && $.getsid(identity) === identity) {
        if ($.kill(identity, 17) !== 0 && $.getsid(identity) === identity) {
            throw new Error("The SSH session owner could not be paused")
        }
    }

    for (const value of processes) {
        if (!/^[0-9]+$/.test(value)) {
            throw new Error("The SSH process identity is invalid")
        }

        const process = Number(value)

        if (!Number.isInteger(process) || process < 1 || process > 2147483647) {
            throw new Error("The SSH process identity is invalid")
        }

        if (process === identity || process === $.getpid() || $.getsid(process) !== identity) {
            continue
        }

        if ($.kill(process, signal) === 0) {
            stopped++
        } else if ($.getsid(process) === identity) {
            throw new Error("The SSH session process could not be stopped")
        }
    }

    if (signal === 9 && $.getsid(identity) === identity) {
        if ($.kill(identity, signal) !== 0) {
            throw new Error("The SSH session owner could not be stopped")
        }

        stopped++
    }

    return String(stopped)
}
