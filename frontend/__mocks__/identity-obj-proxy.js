module.exports = new Proxy({}, {
    get: function(target, name) {
        return name;
    }
});