/*
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @constructor
 * @extends {WebInspector.Object}
 */
WebInspector.TargetManager = function()
{
    WebInspector.Object.call(this);
    /** @type {!Array.<!WebInspector.Target>} */
    this._targets = [];
    /** @type {!Array.<!WebInspector.TargetManager.Observer>} */
    this._observers = [];
    this._observerCapabiliesMaskSymbol = Symbol("observerCapabilitiesMask");
    /** @type {!Object.<string, !Array.<{modelClass: !Function, thisObject: (!Object|undefined), listener: function(!WebInspector.Event)}>>} */
    this._modelListeners = {};
    this._isSuspended = false;
}

WebInspector.TargetManager.Events = {
    InspectedURLChanged: "InspectedURLChanged",
    MainFrameNavigated: "MainFrameNavigated",
    Load: "Load",
    PageReloadRequested: "PageReloadRequested",
    WillReloadPage: "WillReloadPage",
    TargetDisposed: "TargetDisposed",
    SuspendStateChanged: "SuspendStateChanged"
}

WebInspector.TargetManager.prototype = {
    suspendAllTargets: function()
    {
        if (this._isSuspended)
            return;
        this._isSuspended = true;
        this.dispatchEventToListeners(WebInspector.TargetManager.Events.SuspendStateChanged);

        for (var i = 0; i < this._targets.length; ++i) {
            for (var model of this._targets[i].models())
                model.suspendModel();
        }
    },

    /**
     * @return {!Promise}
     */
    resumeAllTargets: function()
    {
        if (!this._isSuspended)
            throw new Error("Not suspended");
        this._isSuspended = false;
        this.dispatchEventToListeners(WebInspector.TargetManager.Events.SuspendStateChanged);

        var promises = [];
        for (var i = 0; i < this._targets.length; ++i) {
            for (var model of this._targets[i].models())
                promises.push(model.resumeModel());
        }
        return Promise.all(promises);
    },

    suspendAndResumeAllTargets: function()
    {
        this.suspendAllTargets();
        this.resumeAllTargets();
    },

    /**
     * @return {boolean}
     */
    allTargetsSuspended: function()
    {
        return this._isSuspended;
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _redispatchEvent: function(event)
    {
        this.dispatchEventToListeners(event.type, event.data);
    },

    /**
     * @param {boolean=} bypassCache
     * @param {string=} injectedScript
     */
    reloadPage: function(bypassCache, injectedScript)
    {
        if (this._targets.length)
            this._targets[0].resourceTreeModel.reloadPage(bypassCache, injectedScript);
    },

    /**
     * @param {!Function} modelClass
     * @param {string} eventType
     * @param {function(!WebInspector.Event)} listener
     * @param {!Object=} thisObject
     */
    addModelListener: function(modelClass, eventType, listener, thisObject)
    {
        for (var i = 0; i < this._targets.length; ++i) {
            var model = this._targets[i].model(modelClass);
            if (model)
                model.addEventListener(eventType, listener, thisObject);
        }
        if (!this._modelListeners[eventType])
            this._modelListeners[eventType] = [];
        this._modelListeners[eventType].push({ modelClass: modelClass, thisObject: thisObject, listener: listener });
    },

    /**
     * @param {!Function} modelClass
     * @param {string} eventType
     * @param {function(!WebInspector.Event)} listener
     * @param {!Object=} thisObject
     */
    removeModelListener: function(modelClass, eventType, listener, thisObject)
    {
        if (!this._modelListeners[eventType])
            return;

        for (var i = 0; i < this._targets.length; ++i) {
            var model = this._targets[i].model(modelClass);
            if (model)
                model.removeEventListener(eventType, listener, thisObject);
        }

        var listeners = this._modelListeners[eventType];
        for (var i = 0; i < listeners.length; ++i) {
            if (listeners[i].modelClass === modelClass && listeners[i].listener === listener && listeners[i].thisObject === thisObject)
                listeners.splice(i--, 1);
        }
        if (!listeners.length)
            delete this._modelListeners[eventType];
    },

    /**
     * @param {!WebInspector.TargetManager.Observer} targetObserver
     * @param {number=} capabilitiesMask
     */
    observeTargets: function(targetObserver, capabilitiesMask)
    {
        if (this._observerCapabiliesMaskSymbol in targetObserver)
            throw new Error("Observer can only be registered once");
        targetObserver[this._observerCapabiliesMaskSymbol] = capabilitiesMask || 0;
        this.targets(capabilitiesMask).forEach(targetObserver.targetAdded.bind(targetObserver));
        this._observers.push(targetObserver);
    },

    /**
     * @param {!WebInspector.TargetManager.Observer} targetObserver
     */
    unobserveTargets: function(targetObserver)
    {
        delete targetObserver[this._observerCapabiliesMaskSymbol];
        this._observers.remove(targetObserver);
    },

    /**
     * @param {string} name
     * @param {number} capabilitiesMask
     * @param {!InspectorBackendClass.Connection} connection
     * @param {?WebInspector.Target} parentTarget
     * @return {!WebInspector.Target}
     */
    createTarget: function(name, capabilitiesMask, connection, parentTarget)
    {
        var target = new WebInspector.Target(this, name, capabilitiesMask, connection, parentTarget);

        /** @type {!WebInspector.ConsoleModel} */
        target.consoleModel = new WebInspector.ConsoleModel(target);
        /** @type {!WebInspector.RuntimeModel} */
        target.runtimeModel = new WebInspector.RuntimeModel(target);

        var networkManager = null;
        if (target.hasNetworkCapability())
            networkManager = new WebInspector.NetworkManager(target);

        var securityOriginManager = WebInspector.SecurityOriginManager.fromTarget(target);
        /** @type {!WebInspector.ResourceTreeModel} */
        target.resourceTreeModel = new WebInspector.ResourceTreeModel(target, networkManager, securityOriginManager);

        if (networkManager)
            new WebInspector.NetworkLog(target, networkManager);

        if (target.hasJSCapability())
            new WebInspector.DebuggerModel(target);

        if (target.hasDOMCapability()) {
            new WebInspector.DOMModel(target);
            new WebInspector.CSSModel(target);
        }

        /** @type {?WebInspector.WorkerManager} */
        target.workerManager = target.hasWorkerCapability() ? new WebInspector.WorkerManager(target) : null;
        /** @type {!WebInspector.CPUProfilerModel} */
        target.cpuProfilerModel = new WebInspector.CPUProfilerModel(target);
        /** @type {!WebInspector.HeapProfilerModel} */
        target.heapProfilerModel = new WebInspector.HeapProfilerModel(target);

        target.tracingManager = new WebInspector.TracingManager(target);

        if (target.hasBrowserCapability())
            target.serviceWorkerManager = new WebInspector.ServiceWorkerManager(target);

        this.addTarget(target);
        return target;
    },

    /**
     * @param {!WebInspector.Target} target
     * @return {!Array<!WebInspector.TargetManager.Observer>}
     */
    _observersForTarget: function(target)
    {
        return this._observers.filter((observer) => target.hasAllCapabilities(observer[this._observerCapabiliesMaskSymbol] || 0));
    },

    /**
     * @param {!WebInspector.Target} target
     */
    addTarget: function(target)
    {
        this._targets.push(target);
        if (this._targets.length === 1) {
            target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.MainFrameNavigated, this._redispatchEvent, this);
            target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.Load, this._redispatchEvent, this);
            target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.PageReloadRequested, this._redispatchEvent, this);
            target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.WillReloadPage, this._redispatchEvent, this);
        }
        var copy = this._observersForTarget(target);
        for (var i = 0; i < copy.length; ++i)
            copy[i].targetAdded(target);

        for (var eventType in this._modelListeners) {
            var listeners = this._modelListeners[eventType];
            for (var i = 0; i < listeners.length; ++i) {
                var model = target.model(listeners[i].modelClass);
                if (model)
                    model.addEventListener(eventType, listeners[i].listener, listeners[i].thisObject);
            }
        }
    },

    /**
     * @param {!WebInspector.Target} target
     */
    removeTarget: function(target)
    {
        this._targets.remove(target);
        if (this._targets.length === 0) {
            target.resourceTreeModel.removeEventListener(WebInspector.ResourceTreeModel.EventTypes.MainFrameNavigated, this._redispatchEvent, this);
            target.resourceTreeModel.removeEventListener(WebInspector.ResourceTreeModel.EventTypes.Load, this._redispatchEvent, this);
            target.resourceTreeModel.removeEventListener(WebInspector.ResourceTreeModel.EventTypes.WillReloadPage, this._redispatchEvent, this);
        }
        var copy = this._observersForTarget(target);
        for (var i = 0; i < copy.length; ++i)
            copy[i].targetRemoved(target);

        for (var eventType in this._modelListeners) {
            var listeners = this._modelListeners[eventType];
            for (var i = 0; i < listeners.length; ++i) {
                var model = target.model(listeners[i].modelClass);
                if (model)
                    model.removeEventListener(eventType, listeners[i].listener, listeners[i].thisObject);
            }
        }
    },

    /**
     * @param {number=} capabilitiesMask
     * @return {!Array.<!WebInspector.Target>}
     */
    targets: function(capabilitiesMask)
    {
        if (!capabilitiesMask)
            return this._targets.slice();
        else
            return this._targets.filter((target) => target.hasAllCapabilities(capabilitiesMask || 0));
    },

    /**
     *
     * @param {number} id
     * @return {?WebInspector.Target}
     */
    targetById: function(id)
    {
        for (var i = 0; i < this._targets.length; ++i) {
            if (this._targets[i].id() === id)
                return this._targets[i];
        }
        return null;
    },

    /**
     * @return {?WebInspector.Target}
     */
    mainTarget: function()
    {
        return this._targets[0] || null;
    },

    __proto__: WebInspector.Object.prototype
}

/**
 * @interface
 */
WebInspector.TargetManager.Observer = function()
{
}

WebInspector.TargetManager.Observer.prototype = {
    /**
     * @param {!WebInspector.Target} target
     */
    targetAdded: function(target) { },

    /**
     * @param {!WebInspector.Target} target
     */
    targetRemoved: function(target) { },
}

/**
 * @type {!WebInspector.TargetManager}
 */
WebInspector.targetManager = new WebInspector.TargetManager();
