local packets = {}

packets.maximumPacket = 1024 * 1024
packets.maximumData = 32768
packets.types = { init = 1, version = 2, open = 3, close = 4, read = 5, write = 6, lstat = 7, fstat = 8, setstat = 9, fsetstat = 10, opendir = 11, readdir = 12, remove = 13, mkdir = 14, rmdir = 15, realpath = 16, stat = 17, rename = 18, readlink = 19, status = 101, handle = 102, data = 103, name = 104, attrs = 105, extended = 200 }

local Reader = {}
Reader.__index = Reader

local Decoder = {}
Decoder.__index = Decoder

local function invalid()
    error({ code = "ssh_sftp_response_invalid", message = "The SFTP response is invalid", detail = "" }, 0)
end

function packets.string(value)
    return string.pack(">I4", #value) .. value
end

function packets.integer(value)
    return string.pack(">I4", value)
end

function packets.offset(value)
    if math.type(value) ~= "integer" or value < 0 then
        error({ code = "ssh_sftp_request_invalid", message = "The SFTP file offset is invalid", detail = "" }, 0)
    end

    return string.pack(">I8", value)
end

function packets.frame(kind, payload)
    return string.pack(">I4B", #payload + 1, kind) .. payload
end

function packets.request(kind, identity, payload)
    return packets.frame(kind, packets.integer(identity) .. payload)
end

function Reader:bytes(count)
    if count > #self.source - self.position + 1 then
        invalid()
    end

    local value = self.source:sub(self.position, self.position + count - 1)
    self.position = self.position + count

    return value
end

function Reader:integer()
    return string.unpack(">I4", self:bytes(4))
end

function Reader:offset()
    local high = self:integer()
    local low = self:integer()

    if high > 0x7fffffff then
        invalid()
    end

    return (high << 32) | low
end

function Reader:string(maximum)
    local count = self:integer()

    if count > maximum then
        invalid()
    end

    return self:bytes(count)
end

function Reader:attributes()
    local flags = self:integer()

    if flags & ~0x8000000f ~= 0 then
        invalid()
    end

    local value = {}

    if flags & 1 ~= 0 then
        value.size = self:offset()
    end

    if flags & 2 ~= 0 then
        value.uid = self:integer()
        value.gid = self:integer()
    end

    if flags & 4 ~= 0 then
        value.permissions = self:integer()
    end

    if flags & 8 ~= 0 then
        value.accessed = self:integer()
        value.modified = self:integer()
    end

    if flags & 0x80000000 ~= 0 then
        local count = self:integer()

        if count > 64 then
            invalid()
        end

        for _ = 1, count do
            self:string(4096)
            self:string(16384)
        end
    end

    return value
end

local function version(reader, value)
    if value.id ~= 3 then
        invalid()
    end

    value.extensions = {}
    local count = 0

    while reader.position <= #reader.source do
        local name = reader:string(256)
        local data = reader:string(4096)
        count = count + 1

        if count > 64 or name == "" or value.extensions[name] ~= nil then
            invalid()
        end

        value.extensions[name] = data
    end
end

local function names(reader, value)
    local count = reader:integer()

    if count > 4096 then
        invalid()
    end

    value.entries = {}

    for index = 1, count do
        local name = reader:string(4096)
        reader:string(16384)
        value.entries[index] = { name = name, attributes = reader:attributes() }
    end
end

function packets.decode(source)
    if #source < 5 or #source > packets.maximumPacket then
        invalid()
    end

    local reader = setmetatable({ source = source, position = 2 }, Reader)
    local value = { type = source:byte(1), id = reader:integer() }

    if value.type == packets.types.version then
        version(reader, value)
    elseif value.type == packets.types.status then
        value.status = reader:integer()
        reader:string(16384)
        reader:string(128)

        if value.status > 8 then
            invalid()
        end
    elseif value.type == packets.types.handle then
        value.handle = reader:string(256)

        if value.handle == "" then
            invalid()
        end
    elseif value.type == packets.types.data then
        value.data = reader:string(packets.maximumData)

        if value.data == "" then
            invalid()
        end
    elseif value.type == packets.types.name then
        names(reader, value)
    elseif value.type == packets.types.attrs then
        value.attributes = reader:attributes()
    else
        invalid()
    end

    if reader.position ~= #source + 1 then
        invalid()
    end

    return value
end

function packets.decoder()
    return setmetatable({ header = "", parts = {}, blocks = {}, blockBytes = 0, bytes = 0 }, Decoder)
end

function Decoder:feed(source, receive)
    local position = 1

    while position <= #source do
        if self.length == nil then
            local count = math.min(4 - #self.header, #source - position + 1)
            self.header = self.header .. source:sub(position, position + count - 1)
            position = position + count

            if #self.header == 4 then
                self.length = string.unpack(">I4", self.header)
                self.header = ""

                if self.length < 5 or self.length > packets.maximumPacket then
                    invalid()
                end
            end
        else
            local count = math.min(self.length - self.bytes, 4096 - self.blockBytes, #source - position + 1)
            self.parts[#self.parts + 1] = source:sub(position, position + count - 1)
            self.blockBytes = self.blockBytes + count
            self.bytes = self.bytes + count
            position = position + count

            if self.blockBytes == 4096 or self.bytes == self.length then
                self.blocks[#self.blocks + 1] = table.concat(self.parts)
                self.parts = {}
                self.blockBytes = 0
            end

            if self.bytes == self.length then
                local body = table.concat(self.blocks)
                self.blocks = {}
                self.bytes = 0
                self.length = nil
                receive(packets.decode(body))
            end
        end
    end
end

function Decoder:complete()
    return self.header == "" and self.length == nil
end

return packets
