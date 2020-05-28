// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @constructor
 * @param {!WebInspector.StylePropertyTreeElement} treeElement
 * @param {!WebInspector.SwatchPopoverHelper} swatchPopoverHelper
 * @param {string} text
 */
WebInspector.BezierPopoverIcon = function(treeElement, swatchPopoverHelper, text)
{
    this._treeElement = treeElement;
    this._swatchPopoverHelper = swatchPopoverHelper;
    this._createDOM(text);

    this._boundBezierChanged = this._bezierChanged.bind(this);
    this._boundOnScroll = this._onScroll.bind(this);
}

WebInspector.BezierPopoverIcon.prototype = {
    /**
     * @return {!Element}
     */
    element: function()
    {
        return this._element;
    },

    /**
     * @param {string} text
     */
    _createDOM: function(text)
    {
        this._element = createElement("nobr");
        this._element.title = WebInspector.UIString("Open cubic bezier editor");

        this._iconElement = this._element.createChild("div", "popover-icon bezier-icon");
        var svg = this._iconElement.createSVGChild("svg");
        svg.setAttribute("height", 10);
        svg.setAttribute("width", 10);
        this._iconElement.addEventListener("click", this._iconClick.bind(this), false);
        var g = svg.createSVGChild("g");
        var path = g.createSVGChild("path");
        path.setAttribute("d", "M2,8 C2,3 8,7 8,2");

        this._bezierValueElement = this._element.createChild("span");
        this._bezierValueElement.textContent = text;
    },

    /**
     * @param {!Event} event
     */
    _iconClick: function(event)
    {
        event.consume(true);
        if (this._swatchPopoverHelper.isShowing()) {
            this._swatchPopoverHelper.hide(true);
            return;
        }

        this._bezierEditor = new WebInspector.BezierEditor();
        var geometry = WebInspector.Geometry.CubicBezier.parse(this._bezierValueElement.textContent);
        this._bezierEditor.setBezier(geometry);
        this._bezierEditor.addEventListener(WebInspector.BezierEditor.Events.BezierChanged, this._boundBezierChanged);
        this._swatchPopoverHelper.show(this._bezierEditor, this._iconElement, this._onPopoverHidden.bind(this));
        this._scrollerElement = this._iconElement.enclosingNodeOrSelfWithClass("style-panes-wrapper");
        if (this._scrollerElement)
            this._scrollerElement.addEventListener("scroll", this._boundOnScroll, false);

        this._originalPropertyText = this._treeElement.property.propertyText;
        this._treeElement.parentPane().setEditingStyle(true);
        var uiLocation = WebInspector.cssWorkspaceBinding.propertyUILocation(this._treeElement.property, false /* forName */);
        if (uiLocation)
            WebInspector.Revealer.reveal(uiLocation, true /* omitFocus */);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _bezierChanged: function(event)
    {
        this._bezierValueElement.textContent = /** @type {string} */ (event.data);
        this._treeElement.applyStyleText(this._treeElement.renderedPropertyText(), false);
    },

    /**
     * @param {!Event} event
     */
    _onScroll: function(event)
    {
        this._swatchPopoverHelper.reposition();
    },

    /**
     * @param {boolean} commitEdit
     */
    _onPopoverHidden: function(commitEdit)
    {
        if (this._scrollerElement)
            this._scrollerElement.removeEventListener("scroll", this._boundOnScroll, false);

        this._bezierEditor.removeEventListener(WebInspector.BezierEditor.Events.BezierChanged, this._boundBezierChanged);
        delete this._bezierEditor;

        var propertyText = commitEdit ? this._treeElement.renderedPropertyText() : this._originalPropertyText;
        this._treeElement.applyStyleText(propertyText, true);
        this._treeElement.parentPane().setEditingStyle(false);
        delete this._originalPropertyText;
    }
}

/**
 * @constructor
 * @param {!WebInspector.StylePropertyTreeElement} treeElement
 * @param {!WebInspector.SwatchPopoverHelper} swatchPopoverHelper
 * @param {string} colorText
 */
WebInspector.ColorSwatchPopoverIcon = function(treeElement, swatchPopoverHelper, colorText)
{
    this._treeElement = treeElement;
    this._treeElement[WebInspector.ColorSwatchPopoverIcon._treeElementSymbol] = this;
    this._swatchPopoverHelper = swatchPopoverHelper;

    this._swatch = WebInspector.ColorSwatch.create();
    this._swatch.setColorText(colorText);
    this._swatch.setFormat(WebInspector.Color.detectColorFormat(this._swatch.color()));
    var shiftClickMessage = WebInspector.UIString("Shift + Click to change color format.");
    this._swatch.iconElement().title = WebInspector.UIString("Open color picker. %s", shiftClickMessage);
    this._swatch.iconElement().addEventListener("click", this._iconClick.bind(this));
    this._contrastColor = null;

    this._boundSpectrumChanged = this._spectrumChanged.bind(this);
    this._boundOnScroll = this._onScroll.bind(this);
}

WebInspector.ColorSwatchPopoverIcon._treeElementSymbol = Symbol("WebInspector.ColorSwatchPopoverIcon._treeElementSymbol");

/**
 * @param {!WebInspector.StylePropertyTreeElement} treeElement
 * @return {?WebInspector.ColorSwatchPopoverIcon}
 */
WebInspector.ColorSwatchPopoverIcon.forTreeElement = function(treeElement)
{
    return treeElement[WebInspector.ColorSwatchPopoverIcon._treeElementSymbol] || null;
}

WebInspector.ColorSwatchPopoverIcon.prototype = {
    /**
     * @return {!Element}
     */
    element: function()
    {
        return this._swatch;
    },

    /**
     * @param {!WebInspector.Color} color
     */
    setContrastColor: function(color)
    {
        this._contrastColor = color;
        if (this._spectrum)
            this._spectrum.setContrastColor(this._contrastColor);
    },

    /**
     * @param {!Event} event
     */
    _iconClick: function(event)
    {
        event.consume(true);
        this.showPopover();
    },

    showPopover: function()
    {
        if (this._swatchPopoverHelper.isShowing()) {
            this._swatchPopoverHelper.hide(true);
            return;
        }

        var color = this._swatch.color();
        var format = this._swatch.format();
        if (format === WebInspector.Color.Format.Original)
            format = color.format();
        this._spectrum = new WebInspector.Spectrum();
        this._spectrum.setColor(color, format);
        if (this._contrastColor)
            this._spectrum.setContrastColor(this._contrastColor);

        this._spectrum.addEventListener(WebInspector.Spectrum.Events.SizeChanged, this._spectrumResized, this);
        this._spectrum.addEventListener(WebInspector.Spectrum.Events.ColorChanged, this._boundSpectrumChanged);
        this._swatchPopoverHelper.show(this._spectrum, this._swatch.iconElement(), this._onPopoverHidden.bind(this));
        this._scrollerElement = this._swatch.enclosingNodeOrSelfWithClass("style-panes-wrapper");
        if (this._scrollerElement)
            this._scrollerElement.addEventListener("scroll", this._boundOnScroll, false);

        this._originalPropertyText = this._treeElement.property.propertyText;
        this._treeElement.parentPane().setEditingStyle(true);
        var uiLocation = WebInspector.cssWorkspaceBinding.propertyUILocation(this._treeElement.property, false /* forName */);
        if (uiLocation)
            WebInspector.Revealer.reveal(uiLocation, true /* omitFocus */);
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _spectrumResized: function(event)
    {
        this._swatchPopoverHelper.reposition();
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _spectrumChanged: function(event)
    {
        var colorString = /** @type {string} */ (event.data);
        this._swatch.setColorText(colorString);
        this._treeElement.applyStyleText(this._treeElement.renderedPropertyText(), false);
    },

    /**
     * @param {!Event} event
     */
    _onScroll: function(event)
    {
        this._swatchPopoverHelper.reposition();
    },

    /**
     * @param {boolean} commitEdit
     */
    _onPopoverHidden: function(commitEdit)
    {
        if (this._scrollerElement)
            this._scrollerElement.removeEventListener("scroll", this._boundOnScroll, false);

        this._spectrum.removeEventListener(WebInspector.Spectrum.Events.ColorChanged, this._boundSpectrumChanged);
        delete this._spectrum;

        var propertyText = commitEdit ? this._treeElement.renderedPropertyText() : this._originalPropertyText;
        this._treeElement.applyStyleText(propertyText, true);
        this._treeElement.parentPane().setEditingStyle(false);
        delete this._originalPropertyText;
    }
}
