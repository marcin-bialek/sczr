/* Systemy Czasu Rzeczywistego
 *
 * Authors: 
 *   Piotr Sawicki (https://github.com/psawicki0)
 *   Marcin Białek (https://github.com/marcin-bialek)
 */

 
const sliders = {} 
const socket = io("/")


function onFrequencyChange(freq, value) {
    const data = {}
    data[freq] = value
    socket.emit("freq_change", data)
}


function sliderOnChange(freq, event) {
    onFrequencyChange(freq, event.target.value)
}


function sliderOnDoubleClick(freq, event) {
    event.target.value = 0
    onFrequencyChange(freq, 0)
}


function makeSlider(freq) {
    const div = document.createElement("div")
    div.classList.add("slider-segment")

    const input = document.createElement("input")
    input.setAttribute("type", "range")
    input.setAttribute("min", "-20")
    input.setAttribute("max", "20")
    input.setAttribute("value", "0")
    input.setAttribute("step", "1")
    input.addEventListener("change", sliderOnChange.bind(null, freq))
    input.addEventListener("dblclick", sliderOnDoubleClick.bind(null, freq))

    const span = document.createElement("span")
    span.innerText = freq < 1000 ? `${ freq } Hz` : `${ Math.floor(freq / 100) / 10 } kHz`

    div.appendChild(input)
    div.appendChild(span)
    return div 
}


socket.on("sliders_init", data => {
    console.log(data)

    const container = document.querySelector(".sliders-container")

    for(const [freq, value] of Object.entries(data)) {
        const segment = makeSlider(freq)
        const slider = segment.querySelector("input")
        slider.value = value 
        sliders[freq] = slider 
        container.appendChild(segment)
    }
}) 


socket.on("freq_change", data => {
    for(const [freq, value] of Object.entries(data)) {
        if(sliders[freq]) {
            console.log(sliders[freq])
            sliders[freq].value = value
        }
    }
})


document.getElementById("bass-boost-botton").addEventListener("click", event => {
    event.target.classList.add("button-animate")
    event.target.disabled = true 

    setTimeout(() => {
        event.target.classList.remove("button-animate")
        event.target.disabled = false 
    }, 2000)

    for(const [freq, slider] of Object.entries(sliders)) {
        const value = freq <= 300 ? slider.max : slider.min
        slider.value = value
        onFrequencyChange(freq, value)
    }
})


document.getElementById("reset-botton").addEventListener("click", event => {
    for(const [freq, slider] of Object.entries(sliders)) {
        if(slider.value !== 0) {
            slider.value = 0
            onFrequencyChange(freq, 0)
        }
    }
})
