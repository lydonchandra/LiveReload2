require 'sugar'
fs   = require 'fs'
Path = require 'path'

{ EventEmitter } = require 'events'

MemoryStream = require 'memorystream'

{ createApiTree }                = require 'apitree'
{ Communicator }                 = require '../lib/communicator'
{ ApiTree, createRemoteApiTree } = require '../lib/remoteapitree'


exports.LRRoot        = LRRoot = Path.join(__dirname, "../..")
exports.LRPluginsRoot = LRPluginsRoot = Path.join(LRRoot, "LiveReload/Compilers")


class CommunicatorTwin extends EventEmitter
  constructor: ->
    @stdin  = new MemoryStream()
    @stdout = new MemoryStream(null, readable: no)
    @stdout.setEncoding('utf8')
    @stderr = new MemoryStream(null, readable: no)
    @stderr.setEncoding('utf8')
    @communicator = new Communicator(@stdin, @stdout, @stderr)
    @communicator.on 'end', =>
      @emit 'end'

  send: (command, data, callback) ->
    @stdin.write JSON.stringify([command, data]) + "\n"
    @communicator.once 'idle', callback

  end: ->
    @stdin.emit 'end'

  toString: -> @stdout.getAll()


exports.CommunicatorTwin = CommunicatorTwin


exports.setup = (modules=[]) ->
  baseDir = Path.join(__dirname, '../app')
  LR = createApiTree baseDir,
    loadItem: (filePath) ->
      result = {}
      for own k, v of require(filePath)
        if typeof v is 'function'
          result[k] = do (k) ->
            -> throw new Error("#{filePath.replace(baseDir+'/', '').replace('.js', '')}.#{k} has been called unexpectedly")
      return result

  LR.mount = new ApiTree().mount

  messages = JSON.parse(fs.readFileSync(Path.join(__dirname, '../config/client-messages.json'), 'utf8'))
  messages.pop()
  LR.client = createRemoteApiTree messages, (msg) ->
    (args...) -> throw new Error("LR.client.#{msg} has been called unexpectedly")

  LR.log =
    fyi: ->
    wtf: (message) -> LR.test.log.push ['wtf', message]
    omg: (message) -> LR.test.log.push ['omg', message]

  LR.test =
    log: []

    import: (modules...) ->
      for module in modules
        LR.mount module, require(Path.join(__dirname, '../app/' + module.replace(/\./g, '/') + '.js'))
      return

    logCall: (name, args...) ->
      callback = (typeof args.last() is 'function') && args.pop()
      LR.test.log.push [name].concat(args)
      callback?(null)

    allow: (apis...) ->
      callback = (typeof apis.last() is 'function') && apis.pop() || LR.test.logCall
      for api in apis
        LR.mount api, callback.fill(api)
      return

    allowRPC: (apis...) ->
      callback = (typeof apis.last() is 'function') && apis.pop() || LR.test.logCall
      for api in apis
        LR.client.mount api, callback.fill("C.#{api}")
      return

  LR.test.import modules...

  global.LR = LR
  return LR
